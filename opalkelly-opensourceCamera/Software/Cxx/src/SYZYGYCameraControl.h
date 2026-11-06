/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// SYZYGYCameraControl.h — AR0330 (SZG-CAMERA) control.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "Axi.h"
#include "CameraTypes.h"
#include "I2CController.h"
#include "ICameraControl.h"
#include "TPGPatterns.h"

namespace okcli {

class SYZYGYCameraControl : public ICameraControl {
public:
    explicit SYZYGYCameraControl(IAxiLite& axiLite)
        : m_i2c(axiLite),
          m_size(kDefaultSize()),
          m_skips{0, 0},
          m_frameDimensions(calculateFrameDimensions(m_size, m_skips)) {}

    MatrixDimensions defaultSize() const override { return kDefaultSize(); }

    std::vector<MatrixDimensions> supportedSkips() const override {
        return {{0, 0}, {1, 1}, {2, 2}};
    }

    std::vector<TestMode> supportedTestModes() const override { return tpgSupportedTestModes(); }

    std::vector<FrameConfiguration> supportedFrameConfigurations() const override {
        std::vector<FrameConfiguration> out;
        for (const MatrixDimensions& skips : supportedSkips()) {
            out.push_back({calculateFrameDimensions(kDefaultSize(), skips), skips});
        }
        return out;
    }

    MatrixDimensions frameDimensions() const override { return m_frameDimensions; }
    CameraExposure exposure() const override { return m_exposure; }

    // Full sensor setup (no pipeline reset). Sensor must be streaming before the pipeline reset
    // so vid_clk (from sensor LVDS) is alive when reconfigurePipeline() resets the AXI-Stream.
    void initialize() override {
        m_i2c.initialize();
        setupOptimizedRegisterSet();
    }

    // Exposure in milliseconds → COARSE_INTEGRATION_TIME row periods (no AEC on this sensor).
    void setExposure(CameraExposure exposure) override {
        m_exposure = exposure;
        const double pixClkNs = 34.0;  // pixel clock period
        const uint16_t lineLengthPck = m_i2c.read16(DEV, REG_LINE_LENGTH_PCK);
        const int exposureLlpck =
            static_cast<int>(std::floor((exposure * 1000000.0) / (lineLengthPck * pixClkNs)));
        m_i2c.write16(DEV, REG_COARSE_INTEGRATION_TIME, static_cast<uint16_t>(exposureLlpck & 0xffff));
    }

    // AR0330 always captures the full sensor area; output resolution is controlled by setSkips.
    void setSize(const MatrixDimensions& /*size*/) override {
        const MatrixDimensions sensorSize = kDefaultSize();
        m_i2c.write16(DEV, REG_X_ADDR_END, static_cast<uint16_t>(sensorSize.columnCount + 6 - 1));
        m_i2c.write16(DEV, REG_Y_ADDR_END, static_cast<uint16_t>(sensorSize.rowCount + 124 - 1));
        m_size = sensorSize;
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void setSkips(const MatrixDimensions& skips) override {
        m_i2c.write16(DEV, REG_X_ODD_INC, oddInc(skips.columnCount, "column"));
        m_i2c.write16(DEV, REG_Y_ODD_INC, oddInc(skips.rowCount, "row"));
        m_skips = skips;
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void reinitializeI2C() override { m_i2c.initialize(); }

private:
    static MatrixDimensions kDefaultSize() { return {1296, 2304}; }  // {row, col}

    static constexpr int DEV = 0x10;  // AR0330 7-bit address (SADDR low)

    static constexpr uint16_t REG_Y_ADDR_END             = 0x3006;
    static constexpr uint16_t REG_X_ADDR_END             = 0x3008;
    static constexpr uint16_t REG_LINE_LENGTH_PCK        = 0x300c;
    static constexpr uint16_t REG_COARSE_INTEGRATION_TIME = 0x3012;
    static constexpr uint16_t REG_MODE_SELECT            = 0x301c;
    static constexpr uint16_t REG_VT_PIX_CLK_DIV         = 0x302a;
    static constexpr uint16_t REG_PRE_PLL_CLK_DIV        = 0x302e;
    static constexpr uint16_t REG_PLL_MULTIPLIER         = 0x3030;
    static constexpr uint16_t REG_OP_PIX_CLK_DIV         = 0x3036;
    static constexpr uint16_t REG_OP_SYS_CLK_DIV         = 0x3038;
    static constexpr uint16_t REG_ANALOG_GAIN            = 0x3060;
    static constexpr uint16_t REG_SMIA_TEST              = 0x3064;
    static constexpr uint16_t REG_DATAPATH_SELECT        = 0x306e;
    static constexpr uint16_t REG_TEST_PATTERN_MODE      = 0x3070;
    static constexpr uint16_t REG_X_ODD_INC              = 0x30a2;
    static constexpr uint16_t REG_Y_ODD_INC              = 0x30a6;
    static constexpr uint16_t REG_DATA_FORMAT_BITS       = 0x31ac;
    static constexpr uint16_t REG_HISPI_CONTROL_STATUS   = 0x31c6;
    static constexpr uint16_t REG_COMPRESSION            = 0x31d0;

    static uint16_t oddInc(int skip, const char* which) {
        switch (skip) {
            case 0: return 1;
            case 1: return 3;
            case 2: return 5;
            default: throw AxiError(std::string("Unsupported ") + which + " skip value " +
                                    std::to_string(skip));
        }
    }

    // 1080p30 register set.
    void setupOptimizedRegisterSet() {
        m_i2c.write16(DEV, REG_HISPI_CONTROL_STATUS, 0x8400);  // hispi_control
        m_i2c.write16(DEV, REG_SMIA_TEST, 0x1802);             // disable embedded data
        m_i2c.write16(DEV, REG_DATA_FORMAT_BITS, 0x0a0a);      // data width
        m_i2c.write16(DEV, REG_COMPRESSION, 0x0000);           // disable compression
        m_i2c.write16(DEV, REG_DATAPATH_SELECT, 0x0210);       // datapath select
        m_i2c.write16(DEV, REG_VT_PIX_CLK_DIV, 0x0005);
        m_i2c.write16(DEV, REG_PRE_PLL_CLK_DIV, 0x0002);
        m_i2c.write16(DEV, REG_PLL_MULTIPLIER, 0x0028);
        m_i2c.write16(DEV, REG_OP_SYS_CLK_DIV, 0x0001);
        m_i2c.write16(DEV, REG_OP_PIX_CLK_DIV, 0x000a);        // op_pix_clk_div (data width)
        m_i2c.write16(DEV, REG_COARSE_INTEGRATION_TIME, 0x0400);  // exposure (400 sensor+lens)
        m_i2c.write16(DEV, REG_ANALOG_GAIN, 0x0018);           // ISO 400
        m_i2c.write16(DEV, REG_TEST_PATTERN_MODE, 0x0000);     // disable test pattern
        m_i2c.write16(DEV, REG_MODE_SELECT, 0x0100);           // enable streaming
    }

    I2CController m_i2c;
    MatrixDimensions m_size;
    MatrixDimensions m_skips;
    MatrixDimensions m_frameDimensions;
    CameraExposure m_exposure = 0;
};

}  // namespace okcli
