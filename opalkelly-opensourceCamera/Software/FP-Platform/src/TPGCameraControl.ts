/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { ICameraControl } from "./ICameraControl";
import {
    CameraExposure,
    FrameConfiguration,
    IMatrixDimensions,
    MatrixDimensions,
    TestMode,
    calculateFrameDimensions
} from "./CameraTypes";

import { TPG_SUPPORTED_TEST_MODES } from "./TPGPatterns";

const TPG_DEFAULT_SIZE: MatrixDimensions = { columnCount: 1920, rowCount: 1080 };

const TPG_RESOLUTIONS: MatrixDimensions[] = [
    { columnCount: 1920, rowCount: 1080 },
    { columnCount: 1280, rowCount: 720 },
    { columnCount: 640, rowCount: 480 }
];

/**
 * Camera control implementation for TPG-only operation (no physical camera connected).
 * Uses the FPGA's built-in Test Pattern Generator to produce video frames.
 */
export class TPGCameraControl implements ICameraControl {
    private _size: IMatrixDimensions;
    private _skips: IMatrixDimensions;
    private _frameDimensions: IMatrixDimensions;

    constructor() {
        this._size = TPG_DEFAULT_SIZE;
        this._skips = { columnCount: 0, rowCount: 0 };
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    // Accessors

    public get defaultSize(): MatrixDimensions {
        return TPG_DEFAULT_SIZE;
    }

    public get supportedSkips(): MatrixDimensions[] {
        return [{ rowCount: 0, columnCount: 0 }];
    }

    public get supportedTestModes(): TestMode[] {
        return TPG_SUPPORTED_TEST_MODES;
    }

    public get supportedFrameConfigurations(): FrameConfiguration[] {
        return TPG_RESOLUTIONS.map((dimensions) => ({
            dimensions,
            skips: { rowCount: 0, columnCount: 0 }
        }));
    }

    public get frameDimensions(): IMatrixDimensions {
        return this._frameDimensions;
    }

    public get exposure(): CameraExposure {
        return 0;
    }

    // Operations

    public async initialize(): Promise<void> {
        // No sensor I2C configuration — TPG generates video internally.
        console.log("TPGCameraControl::initialize() Complete");
    }

    /** No-op — TPG has no physical sensor. */
    public async setExposure(_exposure: CameraExposure): Promise<void> {
    }

    public async setSize(size: MatrixDimensions): Promise<void> {
        this._size = size;
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async setSkips(_skips: MatrixDimensions): Promise<void> {
        this._skips = { rowCount: 0, columnCount: 0 };
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async reinitializeI2C(): Promise<void> {
        // No I2C — nothing to reinitialize.
    }

}
