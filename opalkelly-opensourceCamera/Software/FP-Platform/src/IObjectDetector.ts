/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/**
 * Represents a single detected object with class name, confidence score,
 * and bounding box in the original image coordinate space.
 */
export interface DetectedObject {
    class: string;
    score: number;
    bbox: [number, number, number, number]; // [x, y, width, height]
}

/**
 * Interface for an object detector that can run inference on canvas or raw
 * RGBA pixel data and return detected objects.
 */
export interface IObjectDetector {
    load(modelUrl: string): Promise<void>;
    detect(canvas: HTMLCanvasElement, confidenceThreshold?: number): Promise<DetectedObject[]>;
    detectFromRGBA(rgbaData: Uint8Array, width: number, height: number, confidenceThreshold?: number): Promise<DetectedObject[]>;
    dispose(): void;
}
