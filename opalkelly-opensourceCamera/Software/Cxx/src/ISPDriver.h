/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// ISPDriver.h — Image Signal Processor driver.
//
// Register-access layer for the ISP IP (base 0x4CE00000) with state caching for re-application
// during pipeline reconfiguration.

#pragma once

#include <cstdint>

#include "Axi.h"
#include "IISP.h"

namespace okcli {

class ISPDriver : public IISP {
public:
    explicit ISPDriver(IAxiLite& axiLite) : m_axi(axiLite) {}

    uint32_t rgain() const override { return m_rgain; }
    uint32_t ggain() const override { return m_ggain; }
    uint32_t bgain() const override { return m_bgain; }
    uint32_t awb() const override { return m_awb; }

    // Configure all ISP parameters (gains, AWB threshold, frame dimensions).
    void initialize(uint32_t width, uint32_t height, uint32_t awbThresh, uint32_t rgain,
                    uint32_t ggain, uint32_t bgain) override {
        m_width = width;
        m_height = height;
        m_awb = awbThresh;
        m_rgain = rgain;
        m_ggain = ggain;
        m_bgain = bgain;

        m_axi.write32(ISP_BASE + ISP_AWB_THRESH_REG, awbThresh);
        m_axi.write32(ISP_BASE + ISP_RGAIN_REG, rgain);
        m_axi.write32(ISP_BASE + ISP_GGAIN_REG, ggain);
        m_axi.write32(ISP_BASE + ISP_BGAIN_REG, bgain);
        m_axi.write32(ISP_BASE + ISP_HEIGHT_REG, height);
        m_axi.write32(ISP_BASE + ISP_WIDTH_REG, width);
    }

    // Update RGB color gains (safe while the pipeline is running).
    void setGains(uint32_t rgain, uint32_t ggain, uint32_t bgain) override {
        m_rgain = rgain;
        m_ggain = ggain;
        m_bgain = bgain;
        m_axi.write32(ISP_BASE + ISP_RGAIN_REG, rgain);
        m_axi.write32(ISP_BASE + ISP_GGAIN_REG, ggain);
        m_axi.write32(ISP_BASE + ISP_BGAIN_REG, bgain);
    }

    // Update the AWB threshold (safe while the pipeline is running).
    void setAWBThreshold(uint32_t awb) override {
        m_awb = awb;
        m_axi.write32(ISP_BASE + ISP_AWB_THRESH_REG, awb);
    }

    void start() override {
        uint32_t ctrl = m_axi.read32(ISP_BASE + ISP_CTRL_REG);
        ctrl |= ISP_START | ISP_AUTO_RESTART;
        m_axi.write32(ISP_BASE + ISP_CTRL_REG, ctrl);
    }

    void stop() override {
        uint32_t ctrl = m_axi.read32(ISP_BASE + ISP_CTRL_REG);
        ctrl &= ~(ISP_START | ISP_AUTO_RESTART);
        m_axi.write32(ISP_BASE + ISP_CTRL_REG, ctrl);
    }

private:
    static constexpr uint64_t ISP_BASE           = 0x4ce00000ull;
    static constexpr uint64_t ISP_CTRL_REG       = 0x00;
    static constexpr uint64_t ISP_HEIGHT_REG     = 0x10;
    static constexpr uint64_t ISP_WIDTH_REG      = 0x18;
    static constexpr uint64_t ISP_RGAIN_REG      = 0x20;
    static constexpr uint64_t ISP_GGAIN_REG      = 0x28;
    static constexpr uint64_t ISP_BGAIN_REG      = 0x30;
    static constexpr uint64_t ISP_AWB_THRESH_REG = 0x38;
    static constexpr uint32_t ISP_START          = 1u << 0;
    static constexpr uint32_t ISP_AUTO_RESTART   = 1u << 7;

    IAxiLite& m_axi;
    uint32_t m_width = 0, m_height = 0;
    uint32_t m_rgain = 128, m_ggain = 128, m_bgain = 128, m_awb = 255;
};

}  // namespace okcli
