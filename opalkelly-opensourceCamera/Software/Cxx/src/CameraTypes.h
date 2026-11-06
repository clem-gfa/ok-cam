/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// CameraTypes.h — shared camera value types.

#pragma once

#include <cmath>
#include <cstdint>

namespace okcli {

// Camera operating mode.
enum class CameraMode { SzgCam, Pcam, Tpg };

// Matrix row/column dimensions (a frame size or a skip/binning factor).
struct MatrixDimensions {
    int rowCount = 0;
    int columnCount = 0;
};
using IMatrixDimensions = MatrixDimensions;

// A frame-capture configuration: output dimensions + sensor skip (binning) factors.
struct FrameConfiguration {
    MatrixDimensions dimensions;
    MatrixDimensions skips;
};

// Camera exposure. Units are sensor-specific: OV5640 = AEC luminance target (0..247);
// AR0330 = exposure duration in milliseconds.
using CameraExposure = double;

// Test pattern modes (all produced by the TPG).
enum class TestMode {
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
};

// Effective frame dimensions after applying skip (binning) factors:
//   frameDimension = ceil(size / (2*(skips+1))) * 2
inline MatrixDimensions calculateFrameDimensions(const MatrixDimensions& size,
                                                 const MatrixDimensions& skips) {
    MatrixDimensions out;
    out.columnCount = 2 * static_cast<int>(std::ceil(
                              size.columnCount / (2.0 * (skips.columnCount + 1))));
    out.rowCount = 2 * static_cast<int>(std::ceil(
                           size.rowCount / (2.0 * (skips.rowCount + 1))));
    return out;
}

}  // namespace okcli
