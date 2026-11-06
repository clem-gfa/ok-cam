/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import {
    CameraExposure,
    FrameConfiguration,
    MatrixDimensions,
    TestMode
} from "./CameraTypes";

/**
 * Interface for camera sensor control (TPG, SZG, PCAM).
 *
 * Camera controls are capability bags — they expose what they can do
 * (I2C registers, exposure, resolution/skip settings) but have no
 * knowledge of pipeline sequencing. The CapturePipelineSequencer calls
 * these methods at the right moments in the right order.
 *
 * Pipeline IP configuration (ISP gains, TPG patterns, TPG motion speed)
 * is handled directly via IISP and ITPG.
 */
export interface ICameraControl {
    // Accessors
    readonly defaultSize: MatrixDimensions;
    readonly supportedSkips: MatrixDimensions[];
    readonly supportedTestModes: TestMode[];
    readonly supportedFrameConfigurations: FrameConfiguration[];
    readonly frameDimensions: MatrixDimensions;
    readonly exposure: CameraExposure;

    // Initialization (sensor setup only, no pipeline reset)
    initialize(): Promise<void>;

    // Sensor configuration
    setExposure(exposure: CameraExposure): Promise<void>;
    setSize(dimensions: MatrixDimensions): Promise<void>;
    setSkips(dimensions: MatrixDimensions): Promise<void>;

    // Called by sequencer after system reset to reinitialize the AXI IIC IP
    reinitializeI2C(): Promise<void>;
}
