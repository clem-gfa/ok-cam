/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React, { Component, RefObject } from "react";

import * as tf from "@tensorflow/tfjs"

import { YoloDetector } from "./YoloDetector";
import { IObjectDetector, DetectedObject } from "./IObjectDetector";

import { repackGBRtoRGBA } from "./Utilities";

import "./CameraFrameView.css"

import { IMatrixDimensions } from "./CameraTypes";
import { CapturedFrame } from "./CapturePipelineSequencer";


export enum ObjectDetectionState {
    Initializing,
    Ready,
    Error,
    Active
}

/**
 * Properties for the CameraFrameView component.
 */
export interface CameraFrameViewProps {
    dimensions: IMatrixDimensions
    scaleToFit: boolean
    modelUrl: string
    detector?: IObjectDetector
    onObjectDetectionStateChange?: (state: ObjectDetectionState) => void;
    onFirstInferenceComplete?: () => void;
}

interface CameraFrameViewState {
    detections: DetectedObject[];
}

/**
 * Class representing a canvas view component that renders Bayer row and column
 * matrix data.
 */
class CameraFrameView extends Component<CameraFrameViewProps, CameraFrameViewState> {
    private readonly _canvasRef: RefObject<HTMLCanvasElement>;
    private readonly _containerRef: RefObject<HTMLDivElement>;
    private readonly _overlayRef: RefObject<HTMLDivElement>;

    private readonly _containerResizeObserver: ResizeObserver;

    private _objectDetectionModel?: IObjectDetector;
    private _detectionInProgress = false;
    private _firstInferenceCompleted = false;
    private _lastDetectionTime = 0;
    private _detectionEnabled = false;

    private _cachedImageData: ImageData | null = null;
    private _cachedWidth = 0;
    private _cachedHeight = 0;

    constructor(props: CameraFrameViewProps) {
        super(props);

        this.state = { detections: [] };

        this._canvasRef = React.createRef();
        this._containerRef = React.createRef();
        this._overlayRef = React.createRef();

        // Create the ResizeObserver to handle when the Camera frame container resizes
        this._containerResizeObserver = new ResizeObserver((entries) => {
            for (const entry of entries) {
                const { width, height } = entry.contentRect;
                this.updateCanvasSize(width, height);
            }
        });
    }

    componentDidMount() {
        this.updateCanvasSize(this.props.dimensions.columnCount, this.props.dimensions.rowCount);

        this.clearFrameImage();

        console.log(`CameraFrameView::ScaleToFit = ${this.props.scaleToFit}`);

        if(this.props.scaleToFit && this._containerRef.current) {
            this.updateCanvasSize(this._containerRef.current.clientWidth, this._containerRef.current.clientHeight);

            this._containerResizeObserver.observe(this._containerRef.current);
        }
        else {
            this._containerResizeObserver.disconnect();

            this.updateCanvasSize(this.props.dimensions.columnCount, this.props.dimensions.rowCount);
        }

        this.initializeModel();
    }

    componentWillUnmount(): void {
        this._containerResizeObserver.disconnect();
        this._objectDetectionModel?.dispose();
    }

    componentDidUpdate(
        prevProps: Readonly<CameraFrameViewProps>,
        _prevState: Readonly<NonNullable<unknown>>,
        _snapshot?: NonNullable<unknown>
    ): void {
        if (prevProps.dimensions === this.props.dimensions && prevProps.scaleToFit === this.props.scaleToFit) {
            return;
        }

        console.log(`CameraFrameView::ScaleToFit = ${this.props.scaleToFit}`);

        if(this.props.scaleToFit && this._containerRef.current) {
            this.updateCanvasSize(this._containerRef.current.clientWidth, this._containerRef.current.clientHeight);

            this._containerResizeObserver.observe(this._containerRef.current);
        }
        else {
            this._containerResizeObserver.disconnect();

            this.updateCanvasSize(this.props.dimensions.columnCount, this.props.dimensions.rowCount);
        }

        this.setState({ detections: [] });

        this.clearFrameImage();
    }

    render() {
        return (
            <div
                ref={this._containerRef}
                style={{
                    position: "relative",
                    display: "inline-block",
                    width: "100%",
                    height: "100%",
                    margin: 0,
                    padding: 0,
                    boxSizing: "border-box",
                    overflow: "hidden",
                }}
                >
                <canvas
                    ref={this._canvasRef}
                    style={{
                        position: "absolute",
                        display: "block",
                        left: "0px",
                        right: "0px",
                        margin: 0,
                        padding: 0
                    }}
                />
                <div
                    ref={this._overlayRef}
                    style={{
                        position: "absolute",
                        display: "block",
                        left: "0px",
                        right: "0px",
                        margin: 0,
                        padding: 0
                    }}
                >
                    {this.state.detections.map((obj, i) => (
                        <React.Fragment key={i}>
                            <div
                                className="okBoundingBox"
                                style={{
                                    position: "absolute",
                                    left: `${obj.bbox[0]}px`,
                                    top: `${obj.bbox[1]}px`,
                                    width: `${obj.bbox[2]}px`,
                                    height: `${obj.bbox[3]}px`
                                }}
                            />
                            <p
                                className="okLabel"
                                style={{
                                    position: "absolute",
                                    left: `${obj.bbox[0]}px`,
                                    top: `${obj.bbox[1]}px`,
                                    width: `${obj.bbox[2]}px`
                                }}
                            >
                                {obj.class} &mdash; {Math.round(obj.score * 100)}% confidence
                            </p>
                        </React.Fragment>
                    ))}
                </div>
            </div>
        );
    }

    // Event Handlers
    private updateCanvasSize(width: number, height: number) {
        console.log(`updateCanvasSize: (${width}x${height})`);
        if(this._canvasRef.current) {
            this._canvasRef.current.width = this.props.dimensions.columnCount;
            this._canvasRef.current.height = this.props.dimensions.rowCount;

            // Calculate scale factors for both dimensions
            const scaleX = width / this.props.dimensions.columnCount;
            const scaleY = height / this.props.dimensions.rowCount;

            // Use the smaller scale factor to ensure the image fits completely
            const scale = Math.min(scaleX, scaleY);

            console.log(`updateCanvasSize: [${scaleX}x${scaleY}] => ${scale}`);

            this._canvasRef.current.style.width = "";
            this._canvasRef.current.style.height = "";

            this._canvasRef.current.style.transform = `scale(${scale})`;
            this._canvasRef.current.style.transformOrigin = 'top left';

            if(this._overlayRef.current) {
                this._overlayRef.current.style.width = `${this.props.dimensions.columnCount}px`;
                this._overlayRef.current.style.height = `${this.props.dimensions.rowCount}px`;

                this._overlayRef.current.style.transform = `scale(${scale})`;
                this._overlayRef.current.style.transformOrigin = 'top left';
            }
        }
    }

    /**
     * Clears the frame image on the canvas.
     */
    public clearFrameImage() {
        const canvas: HTMLCanvasElement | null = this._canvasRef.current;

        if (canvas != null) {
            const context: CanvasRenderingContext2D | null = canvas.getContext("2d");

            if (context != null) {
                context.fillStyle = "black";
                context.fillRect(0, 0, canvas.width, canvas.height);
            }
        }
    }

    /**
     * Triggers object detection on the current canvas content without requiring
     * a new frame. Used when detection is enabled in single-capture (picture) mode
     * where no new frames arrive from the continuous capture loop.
     */
    public triggerDetection() {
        if (!this._detectionInProgress) {
            this._detectionEnabled = true;
            this._firstInferenceCompleted = false;
            this._detectionInProgress = true;
            this._lastDetectionTime = performance.now();
            this.runDetection().finally(() => {
                this._detectionInProgress = false;
            });
        }
    }

    /**
     * Updates the frame image on the canvas to display the specified frame.
     * @param frame - The captured frame envelope to render, or null to leave the canvas unchanged.
     * @param enableObjectDetection - Whether to run object detection on the frame.
     */
    public updateFrameImage(frame: CapturedFrame | null, enableObjectDetection: boolean) {
        const canvas: HTMLCanvasElement | null = this._canvasRef.current;
        if (canvas == null) return;

        if (frame != null) {
            const { image, width, height } = frame;

            const context: CanvasRenderingContext2D | null = canvas.getContext("2d", {
                alpha: false
            });

            if (context != null) {
                // Reuse ImageData when dimensions haven't changed to avoid
                // allocating ~8MB per frame and the associated GC pressure.
                if (!this._cachedImageData
                    || this._cachedWidth !== width
                    || this._cachedHeight !== height) {
                    this._cachedImageData = context.createImageData(width, height);
                    this._cachedWidth = width;
                    this._cachedHeight = height;
                }

                repackGBRtoRGBA(image, this._cachedImageData.data, width * height);

                context.putImageData(this._cachedImageData, 0, 0);
            }
        }

        this._detectionEnabled = enableObjectDetection;

        if (!enableObjectDetection) {
            this._firstInferenceCompleted = false;
            if (this.state.detections.length > 0) {
                this.setState({ detections: [] });
            }
        }

        // Throttle detection to at most once every 100ms to reduce GPU contention
        const now = performance.now();
        if (enableObjectDetection && !this._detectionInProgress
            && (now - this._lastDetectionTime) >= 100) {
            this._detectionInProgress = true;
            this._lastDetectionTime = now;
            this.runDetection().finally(() => {
                this._detectionInProgress = false;
            });
        }
    }

    private async runDetection() {
        const model = this._objectDetectionModel;
        if (!model) return;

        const detections = await this.predictObjects(model);

        if (!this._firstInferenceCompleted) {
            this._firstInferenceCompleted = true;
            this.props.onFirstInferenceComplete?.();
        }

        // Discard results if detection was disabled while inference was in-flight
        // Discard results if detection was disabled while inference was in-flight
        if (!this._detectionEnabled) return;

        this.setState({ detections });
    }

    private async initializeModel() {
        this.props.onObjectDetectionStateChange?.(ObjectDetectionState.Initializing);

        try {
            await tf.setBackend('webgl');
            await tf.ready();
            console.log('TensorFlow.js backend initialized:', tf.getBackend());

            const detector = this.props.detector ?? new YoloDetector();
            await detector.load(this.props.modelUrl);
            this._objectDetectionModel = detector;

            console.log('Object detection model loaded successfully');

            this.props.onObjectDetectionStateChange?.(ObjectDetectionState.Ready);
        }
        catch(error) {
            console.error(`CameraFrameView: Failed to initialize object detection model: ${error}`);

            this.props.onObjectDetectionStateChange?.(ObjectDetectionState.Error);
        }
    }

    private async predictObjects(model: IObjectDetector): Promise<DetectedObject[]> {
        try {
            const canvas: HTMLCanvasElement | null = this._canvasRef.current;
            if (canvas != null) {
                return await model.detect(canvas);
            }

            console.error(`CameraFrameView: Canvas reference is null`);
            return [];
        }
        catch(error) {
            console.error(`CameraFrameView: Failed to Predict Objects: ${error}`);
            return [];
        }
    }

}

export default CameraFrameView;
