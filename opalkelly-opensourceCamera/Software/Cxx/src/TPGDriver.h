/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// TPGDriver.h — Video Test Pattern Generator driver.
//
// Register-access layer for the AMD Video TPG IP (base 0x59400000) with state caching.

#pragma once

#include <cstdint>

#include "Axi.h"
#include "ITPG.h"

namespace okcli {

class TPGDriver : public ITPG {
public:
    explicit TPGDriver(IAxiLite& axiLite) : m_axi(axiLite) {}

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    uint32_t patternId() const override { return m_patternId; }
    uint32_t motionSpeed() const override { return m_motionSpeed; }

    void setResolution(uint32_t width, uint32_t height) override {
        m_width = width;
        m_height = height;
        m_axi.write32(TPG_BASE + TPG_ACTIVE_WIDTH_REG, width);
        m_axi.write32(TPG_BASE + TPG_ACTIVE_HEIGHT_REG, height);
    }

    void setPattern(uint32_t patternId) override {
        m_patternId = patternId;
        m_axi.write32(TPG_BASE + TPG_BG_PATTERN_ID_REG, patternId);
    }

    void setMotionSpeed(uint32_t speed) override {
        m_motionSpeed = speed;
        m_axi.write32(TPG_BASE + TPG_MOTION_SPEED_REG, speed);
    }

    // enableInput: true passes sensor input through; false outputs TPG only.
    void start(bool enableInput) override {
        uint32_t ctrl = m_axi.read32(TPG_BASE + TPG_CONTROL_REG);
        ctrl |= TPG_START | TPG_AUTO_RESTART;
        m_axi.write32(TPG_BASE + TPG_CONTROL_REG, ctrl);
        m_axi.write32(TPG_BASE + TPG_ENABLE_INPUT_REG, enableInput ? 1u : 0u);
    }

    void stop() override {
        uint32_t ctrl = m_axi.read32(TPG_BASE + TPG_CONTROL_REG);
        ctrl &= ~(TPG_START | TPG_AUTO_RESTART);
        m_axi.write32(TPG_BASE + TPG_CONTROL_REG, ctrl);
    }

private:
    static constexpr uint64_t TPG_BASE              = 0x59400000ull;
    static constexpr uint64_t TPG_CONTROL_REG       = 0x00;
    static constexpr uint64_t TPG_ACTIVE_HEIGHT_REG = 0x10;
    static constexpr uint64_t TPG_ACTIVE_WIDTH_REG  = 0x18;
    static constexpr uint64_t TPG_BG_PATTERN_ID_REG = 0x20;
    static constexpr uint64_t TPG_MOTION_SPEED_REG  = 0x38;
    static constexpr uint64_t TPG_ENABLE_INPUT_REG  = 0x98;
    static constexpr uint32_t TPG_START             = 1u << 0;
    static constexpr uint32_t TPG_AUTO_RESTART      = 1u << 7;

    IAxiLite& m_axi;
    uint32_t m_width = 0, m_height = 0;
    uint32_t m_patternId = 0x00;
    uint32_t m_motionSpeed = 0x03;
};

}  // namespace okcli
