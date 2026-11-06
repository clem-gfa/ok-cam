/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as tf from "@tensorflow/tfjs";

import { COCO_CLASSES } from "./cocoClasses";
import { IObjectDetector, DetectedObject } from "./IObjectDetector";

/**
 * YOLO object detector using TensorFlow.js graph model backend.
 *
 * Handles letterbox pre-processing, model inference, and NMS post-processing
 * for YOLO models exported via Ultralytics to tfjs format.
 */
export class YoloDetector implements IObjectDetector {
    private _model: tf.GraphModel | null = null;
    private _inputWidth = 640;
    private _inputHeight = 640;
    private _isNHWC = true;

    /**
     * Load a tfjs graph model from the given URL.
     * Inspects input/output shapes to configure pre/post-processing.
     */
    async load(modelUrl: string): Promise<void> {
        this._model = await tf.loadGraphModel(modelUrl);

        // Inspect input shape to determine NHWC vs NCHW
        const inputShape = this._model.inputs[0].shape;
        if (inputShape && inputShape.length === 4) {
            // [batch, H, W, C] for NHWC or [batch, C, H, W] for NCHW
            if (inputShape[1] === 3) {
                // NCHW: [1, 3, 640, 640]
                this._isNHWC = false;
                this._inputHeight = inputShape[2] as number;
                this._inputWidth = inputShape[3] as number;
            } else {
                // NHWC: [1, 640, 640, 3]
                this._isNHWC = true;
                this._inputHeight = inputShape[1] as number;
                this._inputWidth = inputShape[2] as number;
            }
        }

        // Log output info for debugging
        const outputNodes = this._model.outputs.map((o) => `${o.name} shape=${JSON.stringify(o.shape)}`);
        console.log(
            `YoloDetector: Model loaded (input: ${this._inputWidth}x${this._inputHeight}, ` +
            `format: ${this._isNHWC ? "NHWC" : "NCHW"}, outputs: [${outputNodes.join(", ")}])`
        );
    }

    dispose(): void {
        this._model?.dispose();
        this._model = null;
    }

    /**
     * Run object detection on a canvas element.
     * @returns Array of detected objects with class, score, and bbox in original image coordinates.
     */
    async detect(
        canvas: HTMLCanvasElement,
        confidenceThreshold = 0.25
    ): Promise<DetectedObject[]> {
        if (!this._model) {
            throw new Error("YoloDetector: Model not loaded. Call load() first.");
        }

        const origWidth = canvas.width;
        const origHeight = canvas.height;

        const { tensor: inputTensor, scale, padX, padY } = tf.tidy(() => {
            return this.preprocess(canvas);
        });

        return this._runInference(inputTensor, origWidth, origHeight, scale, padX, padY, confidenceThreshold);
    }

    /**
     * Run object detection on raw RGBA pixel data, bypassing the canvas round-trip.
     * Use this when RGBA data is already available (e.g., from GPUFrameProcessor).
     */
    async detectFromRGBA(
        rgbaData: Uint8Array,
        width: number,
        height: number,
        confidenceThreshold = 0.25
    ): Promise<DetectedObject[]> {
        if (!this._model) {
            throw new Error("YoloDetector: Model not loaded. Call load() first.");
        }

        const { tensor: inputTensor, scale, padX, padY } = tf.tidy(() => {
            return this.preprocessFromRGBA(rgbaData, width, height);
        });

        return this._runInference(inputTensor, width, height, scale, padX, padY, confidenceThreshold);
    }

    private async _runInference(
        inputTensor: tf.Tensor4D,
        origWidth: number,
        origHeight: number,
        scale: number,
        padX: number,
        padY: number,
        confidenceThreshold: number
    ): Promise<DetectedObject[]> {
        if (!this._model) return [];

        // Inference — explicitly request the Identity output (raw detections).
        let rawOutput: tf.Tensor;
        try {
            const result = this._model.execute(inputTensor, "Identity");
            rawOutput = Array.isArray(result) ? result[0] : result;
        } finally {
            inputTensor.dispose();
        }

        // Post-process: map boxes to original coordinates
        try {
            return await this.postprocess(
                rawOutput,
                origWidth,
                origHeight,
                scale,
                padX,
                padY,
                confidenceThreshold
            );
        } finally {
            rawOutput.dispose();
        }
    }

    private preprocess(canvas: HTMLCanvasElement): {
        tensor: tf.Tensor4D;
        scale: number;
        padX: number;
        padY: number;
    } {
        const imgTensor = tf.browser.fromPixels(canvas); // [H, W, 3] uint8
        return this._letterboxAndFormat(imgTensor);
    }

    private preprocessFromRGBA(rgbaData: Uint8Array, width: number, height: number): {
        tensor: tf.Tensor4D;
        scale: number;
        padX: number;
        padY: number;
    } {
        // Create a [H, W, 4] tensor from RGBA data, then slice to [H, W, 3] (drop alpha)
        const rgba = tf.tensor3d(rgbaData, [height, width, 4], "int32");
        const imgTensor = rgba.slice([0, 0, 0], [height, width, 3]) as tf.Tensor3D;
        rgba.dispose();
        return this._letterboxAndFormat(imgTensor);
    }

    private _letterboxAndFormat(imgTensor: tf.Tensor3D): {
        tensor: tf.Tensor4D;
        scale: number;
        padX: number;
        padY: number;
    } {
        const [origH, origW] = [imgTensor.shape[0], imgTensor.shape[1]];

        // Compute letterbox scale and padding
        const scale = Math.min(
            this._inputWidth / origW,
            this._inputHeight / origH
        );
        const newW = Math.round(origW * scale);
        const newH = Math.round(origH * scale);
        const padX = (this._inputWidth - newW) / 2;
        const padY = (this._inputHeight - newH) / 2;

        const padTop = Math.floor(padY);
        const padBottom = this._inputHeight - newH - padTop;
        const padLeft = Math.floor(padX);
        const padRight = this._inputWidth - newW - padLeft;

        // Resize
        const resized = tf.image.resizeBilinear(
            imgTensor.expandDims<tf.Tensor4D>(0),
            [newH, newW]
        );

        // Normalize to [0, 1]
        const normalized = resized.div(255.0);

        // Pad to target size (pad with 114/255 ≈ 0.447, standard YOLO gray)
        const padded = normalized.squeeze<tf.Tensor3D>([0]).pad(
            [
                [padTop, padBottom],
                [padLeft, padRight],
                [0, 0]
            ],
            114 / 255
        );

        let finalTensor: tf.Tensor4D;
        if (this._isNHWC) {
            finalTensor = padded.expandDims<tf.Tensor4D>(0); // [1, H, W, 3]
        } else {
            finalTensor = padded
                .transpose<tf.Tensor3D>([2, 0, 1]) // [3, H, W]
                .expandDims<tf.Tensor4D>(0); // [1, 3, H, W]
        }

        return {
            tensor: finalTensor,
            scale,
            padX: Math.floor(padX),
            padY: Math.floor(padY)
        };
    }

    private async postprocess(
        output: tf.Tensor,
        origWidth: number,
        origHeight: number,
        scale: number,
        padX: number,
        padY: number,
        confidenceThreshold: number
    ): Promise<DetectedObject[]> {
        // Ultralytics tfjs export includes built-in NMS.
        // Output shape: [1, N, 6] where each row is [x1, y1, x2, y2, score, class_id]
        // Coordinates are in the 640x640 letterboxed input space.
        const squeezed = output.squeeze<tf.Tensor2D>([0]); // [N, 6]
        const data = await squeezed.data();
        const numDetections = squeezed.shape[0];
        const cols = squeezed.shape[1];
        squeezed.dispose();

        const results: DetectedObject[] = [];
        for (let i = 0; i < numDetections; i++) {
            const offset = i * cols;
            const x1 = data[offset];
            const y1 = data[offset + 1];
            const x2 = data[offset + 2];
            const y2 = data[offset + 3];
            const score = data[offset + 4];
            const classId = data[offset + 5];

            if (score < confidenceThreshold) continue;

            // Reverse letterbox: subtract padding, then divide by scale
            const rx1 = (x1 - padX) / scale;
            const ry1 = (y1 - padY) / scale;
            const rx2 = (x2 - padX) / scale;
            const ry2 = (y2 - padY) / scale;

            // Clamp to original image bounds
            const cx1 = Math.max(0, Math.min(rx1, origWidth));
            const cy1 = Math.max(0, Math.min(ry1, origHeight));
            const cx2 = Math.max(0, Math.min(rx2, origWidth));
            const cy2 = Math.max(0, Math.min(ry2, origHeight));

            const classIdx = Math.round(classId);
            const className = classIdx < COCO_CLASSES.length
                ? COCO_CLASSES[classIdx]
                : `class_${classIdx}`;

            results.push({
                class: className,
                score,
                bbox: [cx1, cy1, cx2 - cx1, cy2 - cy1]
            });
        }

        return results;
    }
}
