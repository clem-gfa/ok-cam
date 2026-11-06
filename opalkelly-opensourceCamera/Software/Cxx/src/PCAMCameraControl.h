/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// PCAMCameraControl.h — OV5640 (Digilent Pcam, SZG-MIPI-8320) control.
// Uses the pcam bitfile with TPG in passthrough mode.

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Axi.h"
#include "CameraTypes.h"
#include "I2CController.h"
#include "ICameraControl.h"
#include "TPGPatterns.h"

namespace okcli {

class PCAMCameraControl : public ICameraControl {
public:
    explicit PCAMCameraControl(IAxiLite& axiLite)
        : m_i2c(axiLite),
          m_size(kDefaultSize()),
          m_skips{0, 0},
          m_frameDimensions(calculateFrameDimensions(m_size, m_skips)) {}

    MatrixDimensions defaultSize() const override { return kDefaultSize(); }

    std::vector<MatrixDimensions> supportedSkips() const override { return {{0, 0}}; }

    std::vector<TestMode> supportedTestModes() const override { return tpgSupportedTestModes(); }

    std::vector<FrameConfiguration> supportedFrameConfigurations() const override {
        std::vector<FrameConfiguration> out;
        for (const MatrixDimensions& d : {MatrixDimensions{1080, 1920}, MatrixDimensions{720, 1280}}) {
            out.push_back({d, {0, 0}});
        }
        return out;
    }

    MatrixDimensions frameDimensions() const override { return m_frameDimensions; }
    CameraExposure exposure() const override { return m_exposure; }

    void initialize() override {
        m_i2c.initialize();
        ov5640Init();
        ov5640AwbInit();
        ov5640Setup1080p();  // configure 1080p and power up the sensor
        // Reset tracked size to match the 1080p we just configured (avoids setSize() no-op skip).
        m_size = kDefaultSize();
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    // AEC luminance target (0..247). Power-cycle around the writes so the AEC latches and the
    // MIPI link is re-established after a pipeline reset.
    void setExposure(CameraExposure exposure) override {
        m_exposure = exposure;
        const int v = std::max(0, std::min(247, static_cast<int>(exposure)));
        w8(REG_SYSTEM_CTRL, 0x42);                          // power down
        w8(REG_AEC_CTRL0F, static_cast<uint8_t>(v + 8));    // max enter
        w8(REG_AEC_CTRL10, static_cast<uint8_t>(v));        // min enter
        w8(REG_AEC_CTRL1B, static_cast<uint8_t>(v + 8));    // max go out
        w8(REG_AEC_CTRL1E, static_cast<uint8_t>(v));        // min go out
        w8(REG_SYSTEM_CTRL, 0x02);                          // power on
    }

    void setSize(const MatrixDimensions& size) override {
        const bool changed =
            size.columnCount != m_size.columnCount || size.rowCount != m_size.rowCount;
        if (changed) ov5640SetupResolution(size.columnCount, size.rowCount);
        m_size = size;
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void setSkips(const MatrixDimensions& /*skips*/) override {
        m_skips = {0, 0};  // OV5640 does not support skip-based subsampling
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void reinitializeI2C() override { m_i2c.initialize(); }

private:
    static MatrixDimensions kDefaultSize() { return {1080, 1920}; }  // {row, col}

    static constexpr int DEV = 0x3c;  // OV5640 7-bit address

    static constexpr uint16_t REG_SYSTEM_CTRL = 0x3008;  // [7]=SW reset [6]=SW power down
    static constexpr uint16_t REG_AEC_CTRL0F  = 0x3a0f;
    static constexpr uint16_t REG_AEC_CTRL10  = 0x3a10;
    static constexpr uint16_t REG_AEC_CTRL1B  = 0x3a1b;
    static constexpr uint16_t REG_AEC_CTRL1E  = 0x3a1e;

    void w8(uint16_t reg, uint8_t data) { m_i2c.write8(DEV, reg, data); }

    void ov5640SetupResolution(int width, int height) {
        if (width == 1920 && height == 1080) ov5640Setup1080p();
        else if (width == 1280 && height == 720) ov5640Setup720p();
        else throw AxiError("Unsupported OV5640 resolution: " + std::to_string(width) + "x" +
                            std::to_string(height));
    }

    // Base sensor init. Sensor remains powered down after.
    void ov5640Init() {
        const uint16_t seq[][2] = {
            {0x3008, 0x42}, {0x3103, 0x03}, {0x3017, 0x00}, {0x3018, 0x00}, {0x3034, 0x18},
            {0x3035, 0x11}, {0x3036, 0x38}, {0x3037, 0x11}, {0x3108, 0x01}, {0x303d, 0x10},
            {0x303b, 0x19}, {0x3630, 0x2e}, {0x3631, 0x0e}, {0x3632, 0xe2}, {0x3633, 0x23},
            {0x3621, 0xe0}, {0x3704, 0xa0}, {0x3703, 0x5a}, {0x3715, 0x78}, {0x3717, 0x01},
            {0x370b, 0x60}, {0x3705, 0x1a}, {0x3905, 0x02}, {0x3906, 0x10}, {0x3901, 0x0a},
            {0x3731, 0x02}, {0x3600, 0x37}, {0x3601, 0x33}, {0x302d, 0x60}, {0x3620, 0x52},
            {0x371b, 0x20}, {0x471c, 0x50}, {0x3a13, 0x43}, {0x3a18, 0x00}, {0x3a19, 0xf8},
            {0x3635, 0x13}, {0x3636, 0x06}, {0x3634, 0x44}, {0x3622, 0x01}, {0x3c01, 0x34},
            {0x3c04, 0x28}, {0x3c05, 0x98}, {0x3c06, 0x00}, {0x3c07, 0x08}, {0x3c08, 0x00},
            {0x3c09, 0x1c}, {0x3c0a, 0x9c}, {0x3c0b, 0x40}, {0x503d, 0x00}, {0x3820, 0x46},
            {0x300e, 0x45}, {0x4800, 0x14}, {0x302e, 0x08}, {0x4300, 0x6f}, {0x501f, 0x01},
            {0x4713, 0x03}, {0x4407, 0x04}, {0x440e, 0x00}, {0x460b, 0x35}, {0x460c, 0x20},
            {0x3824, 0x01}, {0x5000, 0x07}, {0x5001, 0x03},
        };
        for (const auto& kv : seq) w8(kv[0], static_cast<uint8_t>(kv[1]));
    }

    // AWB init. Does not enable AWB; powers on at the end.
    void ov5640AwbInit() {
        w8(0x3008, 0x42);  // power down
        const uint16_t seq[][2] = {
            {0x3406, 0x00}, {0x5192, 0x04}, {0x5191, 0xf8}, {0x518d, 0x26}, {0x518f, 0x42},
            {0x518e, 0x2b}, {0x5190, 0x42}, {0x518b, 0xd0}, {0x518c, 0xbd}, {0x5187, 0x18},
            {0x5188, 0x18}, {0x5189, 0x56}, {0x518a, 0x5c}, {0x5186, 0x1c}, {0x5181, 0x50},
            {0x5184, 0x20}, {0x5182, 0x11}, {0x5183, 0x00},
        };
        for (const auto& kv : seq) w8(kv[0], static_cast<uint8_t>(kv[1]));
        w8(0x3008, 0x02);  // power on
    }

    // 1920x1080 30fps.
    void ov5640Setup1080p() {
        w8(0x3008, 0x42);  // power down
        w8(0x3035, 0x21); w8(0x3036, 0x69); w8(0x3037, 0x05); w8(0x3108, 0x11); w8(0x3034, 0x1a);
        // crop window (336,426)-(2287,1529)
        w8(0x3800, (336 >> 8) & 0x0f); w8(0x3801, 336 & 0xff);
        w8(0x3802, (426 >> 8) & 0x07); w8(0x3803, 426 & 0xff);
        w8(0x3804, (2287 >> 8) & 0x0f); w8(0x3805, 2287 & 0xff);
        w8(0x3806, (1529 >> 8) & 0x07); w8(0x3807, 1529 & 0xff);
        // output offset (16,12)
        w8(0x3810, (16 >> 8) & 0x0f); w8(0x3811, 16 & 0xff);
        w8(0x3812, (12 >> 8) & 0x07); w8(0x3813, 12 & 0xff);
        // output size 1920x1080
        w8(0x3808, (1920 >> 8) & 0x0f); w8(0x3809, 1920 & 0xff);
        w8(0x380a, (1080 >> 8) & 0x7f); w8(0x380b, 1080 & 0xff);
        // timing HTS=2500 VTS=1120
        w8(0x380c, (2500 >> 8) & 0x1f); w8(0x380d, 2500 & 0xff);
        w8(0x380e, (1120 >> 8) & 0xff); w8(0x380f, 1120 & 0xff);
        // no binning, no mirror
        w8(0x3814, 0x11); w8(0x3815, 0x11); w8(0x3821, 0x00);
        // MIPI pclk period, analog path, output format
        w8(0x4837, 24); w8(0x3618, 0x00); w8(0x3612, 0x59); w8(0x3708, 0x64); w8(0x3709, 0x52);
        w8(0x370c, 0x03); w8(0x4300, 0x00); w8(0x501f, 0x03);  // RGB output via ISP
        w8(0x3008, 0x02);  // power on
    }

    // 1280x720 60fps.
    void ov5640Setup720p() {
        w8(0x3008, 0x42);  // power down
        w8(0x3035, 0x21); w8(0x3036, 0x46); w8(0x3037, 0x05); w8(0x3108, 0x11); w8(0x3034, 0x1a);
        // crop window (0,8)-(2619,1947)
        w8(0x3800, (0 >> 8) & 0x0f); w8(0x3801, 0 & 0xff);
        w8(0x3802, (8 >> 8) & 0x07); w8(0x3803, 8 & 0xff);
        w8(0x3804, (2619 >> 8) & 0x0f); w8(0x3805, 2619 & 0xff);
        w8(0x3806, (1947 >> 8) & 0x07); w8(0x3807, 1947 & 0xff);
        // output offset (0,0)
        w8(0x3810, (0 >> 8) & 0x0f); w8(0x3811, 0 & 0xff);
        w8(0x3812, (0 >> 8) & 0x07); w8(0x3813, 0 & 0xff);
        // output size 1280x720
        w8(0x3808, (1280 >> 8) & 0x0f); w8(0x3809, 1280 & 0xff);
        w8(0x380a, (720 >> 8) & 0x7f); w8(0x380b, 720 & 0xff);
        // timing HTS=1896 VTS=984
        w8(0x380c, (1896 >> 8) & 0x1f); w8(0x380d, 1896 & 0xff);
        w8(0x380e, (984 >> 8) & 0xff); w8(0x380f, 984 & 0xff);
        // 3:1 binning both directions, vertical mirror
        w8(0x3814, 0x31); w8(0x3815, 0x31); w8(0x3821, 0x01);
        w8(0x4837, 36); w8(0x3618, 0x00); w8(0x3612, 0x59); w8(0x3708, 0x64); w8(0x3709, 0x52);
        w8(0x370c, 0x03); w8(0x4300, 0x00); w8(0x501f, 0x03);  // RGB output via ISP
        w8(0x3008, 0x02);  // power on
    }

    I2CController m_i2c;
    MatrixDimensions m_size;
    MatrixDimensions m_skips;
    MatrixDimensions m_frameDimensions;
    CameraExposure m_exposure = 0;
};

}  // namespace okcli
