/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { TestMode } from "./CameraTypes";

// TPG pattern ID constants (from AMD v_tpg IP)
// Note: Color Bars (0x09) and Checkerboard (0x0F) are NOT available —
// gateware configures v_tpg with CONFIG.COLOR_BAR {0}.
export const TPG_PATTERN_PASSTHROUGH = 0x00;
export const TPG_PATTERN_HORIZONTAL_RAMP = 0x01;
export const TPG_PATTERN_VERTICAL_RAMP = 0x02;
export const TPG_PATTERN_TEMPORAL_RAMP = 0x03;
export const TPG_PATTERN_SOLID_RED = 0x04;
export const TPG_PATTERN_SOLID_GREEN = 0x05;
export const TPG_PATTERN_SOLID_BLUE = 0x06;
export const TPG_PATTERN_SOLID_BLACK = 0x07;
export const TPG_PATTERN_SOLID_WHITE = 0x08;
export const TPG_PATTERN_COMBINED_RAMP = 0x0e;
export const TPG_PATTERN_PSEUDORANDOM = 0x10;
export const TPG_PATTERN_DP_COLOR_RAMP = 0x11;
export const TPG_PATTERN_DP_BW_VERTICAL = 0x12;
export const TPG_PATTERN_DP_COLOR_SQUARE = 0x13;

/**
 * The set of TestMode values supported by the TPG.
 * Shared by TPGCamera and PCAMCamera (both route test patterns through the TPG).
 */
export const TPG_SUPPORTED_TEST_MODES: TestMode[] = [
    TestMode.HorizontalRamp,
    TestMode.VerticalRamp,
    TestMode.TemporalRamp,
    TestMode.SolidRed,
    TestMode.SolidGreen,
    TestMode.SolidBlue,
    TestMode.SolidBlack,
    TestMode.SolidWhite,
    TestMode.CombinedRamp,
    TestMode.Pseudorandom,
    TestMode.DPColorRamp,
    TestMode.DPBWVertical,
    TestMode.DPColorSquare,
];

/**
 * Maps a TestMode enum value to its corresponding TPG pattern ID.
 * Returns the specified fallbackPattern for unrecognized modes.
 */
export function testModeToPatternId(mode: TestMode, fallbackPattern: number): number {
    switch (mode) {
        case TestMode.HorizontalRamp:
            return TPG_PATTERN_HORIZONTAL_RAMP;
        case TestMode.VerticalRamp:
            return TPG_PATTERN_VERTICAL_RAMP;
        case TestMode.TemporalRamp:
            return TPG_PATTERN_TEMPORAL_RAMP;
        case TestMode.SolidRed:
            return TPG_PATTERN_SOLID_RED;
        case TestMode.SolidGreen:
            return TPG_PATTERN_SOLID_GREEN;
        case TestMode.SolidBlue:
            return TPG_PATTERN_SOLID_BLUE;
        case TestMode.SolidBlack:
            return TPG_PATTERN_SOLID_BLACK;
        case TestMode.SolidWhite:
            return TPG_PATTERN_SOLID_WHITE;
        case TestMode.CombinedRamp:
            return TPG_PATTERN_COMBINED_RAMP;
        case TestMode.Pseudorandom:
            return TPG_PATTERN_PSEUDORANDOM;
        case TestMode.DPColorRamp:
            return TPG_PATTERN_DP_COLOR_RAMP;
        case TestMode.DPBWVertical:
            return TPG_PATTERN_DP_BW_VERTICAL;
        case TestMode.DPColorSquare:
            return TPG_PATTERN_DP_COLOR_SQUARE;
        default:
            return fallbackPattern;
    }
}
