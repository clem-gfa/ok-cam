/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// TPGPatterns.h — AMD v_tpg pattern IDs + TestMode mapping.
//
// Color Bars (0x09) and Checkerboard (0x0F) are NOT available — the gateware builds v_tpg with
// CONFIG.COLOR_BAR {0}.

#pragma once

#include <cstdint>
#include <vector>

#include "CameraTypes.h"

namespace okcli {

// The TestMode values supported by the TPG. Shared by TPGCamera and PCAMCamera/SYZYGYCamera
// (all route test patterns through the TPG).
inline const std::vector<TestMode>& tpgSupportedTestModes() {
    static const std::vector<TestMode> modes = {
        TestMode::HorizontalRamp, TestMode::VerticalRamp, TestMode::TemporalRamp,
        TestMode::SolidRed, TestMode::SolidGreen, TestMode::SolidBlue, TestMode::SolidBlack,
        TestMode::SolidWhite, TestMode::CombinedRamp, TestMode::Pseudorandom,
        TestMode::DPColorRamp, TestMode::DPBWVertical, TestMode::DPColorSquare};
    return modes;
}

constexpr uint32_t TPG_PATTERN_PASSTHROUGH     = 0x00;
constexpr uint32_t TPG_PATTERN_HORIZONTAL_RAMP = 0x01;
constexpr uint32_t TPG_PATTERN_VERTICAL_RAMP   = 0x02;
constexpr uint32_t TPG_PATTERN_TEMPORAL_RAMP   = 0x03;
constexpr uint32_t TPG_PATTERN_SOLID_RED       = 0x04;
constexpr uint32_t TPG_PATTERN_SOLID_GREEN     = 0x05;
constexpr uint32_t TPG_PATTERN_SOLID_BLUE      = 0x06;
constexpr uint32_t TPG_PATTERN_SOLID_BLACK     = 0x07;
constexpr uint32_t TPG_PATTERN_SOLID_WHITE     = 0x08;
constexpr uint32_t TPG_PATTERN_COMBINED_RAMP   = 0x0e;
constexpr uint32_t TPG_PATTERN_PSEUDORANDOM    = 0x10;
constexpr uint32_t TPG_PATTERN_DP_COLOR_RAMP   = 0x11;
constexpr uint32_t TPG_PATTERN_DP_BW_VERTICAL  = 0x12;
constexpr uint32_t TPG_PATTERN_DP_COLOR_SQUARE = 0x13;

// Map a TestMode to its TPG pattern ID; returns fallbackPattern for unrecognized modes.
inline uint32_t testModeToPatternId(TestMode mode, uint32_t fallbackPattern) {
    switch (mode) {
        case TestMode::HorizontalRamp: return TPG_PATTERN_HORIZONTAL_RAMP;
        case TestMode::VerticalRamp:   return TPG_PATTERN_VERTICAL_RAMP;
        case TestMode::TemporalRamp:   return TPG_PATTERN_TEMPORAL_RAMP;
        case TestMode::SolidRed:       return TPG_PATTERN_SOLID_RED;
        case TestMode::SolidGreen:     return TPG_PATTERN_SOLID_GREEN;
        case TestMode::SolidBlue:      return TPG_PATTERN_SOLID_BLUE;
        case TestMode::SolidBlack:     return TPG_PATTERN_SOLID_BLACK;
        case TestMode::SolidWhite:     return TPG_PATTERN_SOLID_WHITE;
        case TestMode::CombinedRamp:   return TPG_PATTERN_COMBINED_RAMP;
        case TestMode::Pseudorandom:   return TPG_PATTERN_PSEUDORANDOM;
        case TestMode::DPColorRamp:    return TPG_PATTERN_DP_COLOR_RAMP;
        case TestMode::DPBWVertical:   return TPG_PATTERN_DP_BW_VERTICAL;
        case TestMode::DPColorSquare:  return TPG_PATTERN_DP_COLOR_SQUARE;
        default:                       return fallbackPattern;
    }
}

}  // namespace okcli
