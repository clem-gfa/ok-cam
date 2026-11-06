/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/** Discriminant for the camera operating mode. */
export type CameraMode = "szgcam" | "pcam" | "tpg";

/**
 * Interface for matrix row and column dimensions.
 */
export interface IMatrixDimensions {
    rowCount: number;
    columnCount: number;
}

/**
 * Type representing the configuration for a frame capture operation. The configuration
 * specifies the row and column dimensions of the frame to capture and skips specifies
 * the number of rows and columns of the sensor to skip for each pixel.
 */
export type FrameConfiguration = {
    dimensions: MatrixDimensions;
    skips: MatrixDimensions;
};

/**
 * Enumeration for the different test modes that can be enabled on the camera.
 */
export enum TestMode {
    HorizontalRamp,
    VerticalRamp,
    TemporalRamp,
    SolidRed,
    SolidGreen,
    SolidBlue,
    SolidBlack,
    SolidWhite,
    CombinedRamp,
    Pseudorandom,
    DPColorRamp,
    DPBWVertical,
    DPColorSquare,
}

/**
 * Type representing a camera exposure value. Units are sensor-specific:
 * OV5640 (PCAM): AEC luminance target (0–247, unitless brightness scale).
 * AR0330 (SYZYGY): exposure duration in milliseconds.
 */
export type CameraExposure = number;

/**
 * How the exposure control should present itself. Because the units above are
 * sensor-specific, a single label cannot honestly describe both sensors — the label and the
 * readout follow the camera that is actually attached.
 *
 * Mirrors ExposureUi in the C++ application (Software/Cxx/gui/main_gui.cpp); keep the two in
 * sync.
 */
export type ExposureUi =
    | "shutter" // AR0330: the stops are genuine shutter speeds, shown as 1/x
    | "aec" // OV5640: the value is a brightness setpoint, shown as its raw 0..247
    | "none"; // no sensor (TPG); the control is disabled anyway

export function exposureUiFor(mode: CameraMode): ExposureUi {
    switch (mode) {
        case "szgcam":
            return "shutter";
        case "pcam":
            return "aec";
        case "tpg":
            return "none";
    }
}

/** Group-label text for each presentation. */
export function exposureTitleFor(ui: ExposureUi): string {
    switch (ui) {
        case "shutter":
            return "Exposure (1/s)";
        case "aec":
            return "Brightness (AEC target)";
        case "none":
            return "Exposure";
    }
}

export type RedGain = number;

export type GreenGain = number;

export type BlueGain = number;

export type AWB = number;

/**
 * Type representing the row and column dimensions of a matrix.
 */
export type MatrixDimensions = IMatrixDimensions;

/**
 * Calculate effective frame dimensions after applying skip (binning) factors.
 * frameDimension = ceil(size / (2 * (skips + 1))) * 2
 */
export function calculateFrameDimensions(
    size: MatrixDimensions,
    skips: MatrixDimensions
): MatrixDimensions {
    return {
        columnCount: 2 * Math.ceil(size.columnCount / (2 * (skips.columnCount + 1))),
        rowCount: 2 * Math.ceil(size.rowCount / (2 * (skips.rowCount + 1)))
    };
}

