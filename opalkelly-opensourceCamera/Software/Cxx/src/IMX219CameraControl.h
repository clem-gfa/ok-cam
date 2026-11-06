/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// IMX219CameraControl.h — Sony IMX219 (Raspberry Pi Camera Module 2) control.
//
// Used with the XEM8310-AU25P "IMX219" bitfile (camera_imx219.bit): CSI-2 RX (2 lanes, RAW10)
// -> ISP -> TPG (passthrough) -> VDMA, like the OV5640 design. The AXI IIC IP is shared with the
// other sensors (I2CController).
//
// Register values follow the Linux kernel driver drivers/media/i2c/imx219.c (2-lane mode,
// 24 MHz INCK, 1920x1080 cropped mode) and the IMX219PQH5-C datasheet:
//   - pixel rate 182.4 Mpix/s (VT PLL 456 MHz), 912 Mb/s per lane (OP PLL 912 MHz);
//   - 1920x1080: centred crop (680,692)-(2599,1771) of the active array, no binning;
//   - LINE_LENGTH_A = 3448, FRAME_LENGTH_A = 1763  -> 30 fps;
//   - default orientation (no flip) gives an RGGB Bayer pattern.
//
// The module has its own 24 MHz oscillator (INCK is not on the 15-pin FFC).
// GPIO0 (FFC pin 11, driven by pcam_power_en = AXI IIC GPO, default 1) must be high to enable
// the sensor before I2C access.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Axi.h"
#include "CameraTypes.h"
#include "I2CController.h"
#include "ICameraControl.h"
#include "TPGPatterns.h"

namespace okcli {

class IMX219CameraControl : public ICameraControl {
public:
    explicit IMX219CameraControl(IAxiLite& axiLite)
        : m_i2c(axiLite),
          m_size(kDefaultSize()),
          m_skips{0, 0},
          m_frameDimensions(calculateFrameDimensions(m_size, m_skips)) {}

    MatrixDimensions defaultSize() const override { return kDefaultSize(); }

    std::vector<MatrixDimensions> supportedSkips() const override { return {{0, 0}}; }

    std::vector<TestMode> supportedTestModes() const override { return tpgSupportedTestModes(); }

    std::vector<FrameConfiguration> supportedFrameConfigurations() const override {
        return {{kDefaultSize(), {0, 0}}};
    }

    MatrixDimensions frameDimensions() const override { return m_frameDimensions; }
    CameraExposure exposure() const override { return m_exposure; }

    // Full sensor setup; leaves the sensor streaming (as the OV5640 control does). The sensor's
    // MIPI clock lane is non-continuous (LP-11 between bursts), so the CSI-2 RX can be reset
    // afterwards by the sequencer and re-synchronises on the next frame start.
    void initialize() override {
        m_i2c.initialize();

        const uint16_t id = m_i2c.read16(DEV, REG_CHIP_ID);
        if (id != CHIP_ID) {
            char b[16];
            std::snprintf(b, sizeof(b), "0x%04x", id);
            throw AxiError(std::string("IMX219 not found on I2C 0x10 (chip id ") + b +
                           ", expected 0x0219). Check the camera cable, GPIO0 and VIO2 = 1.8 V.");
        }

        w8(REG_MODE_SELECT, 0x00);  // software standby

        // Access to the manufacturer-specific registers (datasheet table 11).
        w8(0x30eb, 0x05); w8(0x30eb, 0x0c); w8(0x300a, 0xff); w8(0x300b, 0xff);
        w8(0x30eb, 0x05); w8(0x30eb, 0x09);

        // Undocumented registers (kernel driver).
        const uint16_t undoc[][2] = {
            {0x455e, 0x00}, {0x471e, 0x4b}, {0x4767, 0x0f}, {0x4750, 0x14}, {0x4540, 0x00},
            {0x47b4, 0x14}, {0x4713, 0x30}, {0x478b, 0x10}, {0x478f, 0x10}, {0x4793, 0x10},
            {0x4797, 0x0e}, {0x479b, 0x0e},
        };
        for (const auto& kv : undoc) w8(kv[0], static_cast<uint8_t>(kv[1]));

        w8(REG_X_ODD_INC_A, 1);
        w8(REG_Y_ODD_INC_A, 1);

        // Output setup: automatic D-PHY timing, INCK = 24 MHz.
        w8(REG_DPHY_CTRL, 0x00);
        w16(REG_EXCK_FREQ, 24 * 256);

        // 2-lane PLL setup: VT 456 MHz (182.4 Mpix/s), OP 912 MHz (912 Mb/s per lane).
        w8(REG_VTPXCK_DIV, 5);
        w8(REG_VTSYCK_DIV, 1);
        w8(REG_PREPLLCK_VT_DIV, 3);
        w8(REG_PREPLLCK_OP_DIV, 3);
        w16(REG_PLL_VT_MPY, 57);
        w8(REG_OPSYCK_DIV, 1);
        w16(REG_PLL_OP_MPY, 114);
        w8(REG_CSI_LANE_MODE, 0x01);  // 2 lanes

        setupMode1080p();

        // Exposure / gain / orientation defaults.
        w8(REG_ORIENTATION, 0x00);  // no flip -> RGGB
        w8(REG_ANALOG_GAIN, 0);
        w16(REG_DIGITAL_GAIN, 0x0100);
        setExposure(m_exposure > 0 ? m_exposure : kDefaultExposureMs);

        w8(REG_MODE_SELECT, 0x01);  // streaming

        m_size = kDefaultSize();
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    // Exposure in milliseconds -> COARSE_INTEGRATION_TIME in line periods (no AEC on this sensor).
    void setExposure(CameraExposure exposure) override {
        m_exposure = exposure;
        const double lineUs = LINE_LENGTH_PCK / PIXEL_RATE_HZ * 1e6;
        long lines = std::lround((exposure * 1000.0) / lineUs);
        lines = std::max<long>(EXPOSURE_MIN_LINES, std::min<long>(FRAME_LENGTH_LINES - 4, lines));
        w16(REG_COARSE_INTEGRATION_TIME, static_cast<uint16_t>(lines));
    }

    // Only the cropped 1920x1080 mode is supported for now (the ISP is built for 1920x1080).
    void setSize(const MatrixDimensions& size) override {
        if (size.columnCount != 1920 || size.rowCount != 1080) {
            throw AxiError("Unsupported IMX219 resolution: " + std::to_string(size.columnCount) +
                           "x" + std::to_string(size.rowCount));
        }
        m_size = size;
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void setSkips(const MatrixDimensions& /*skips*/) override {
        m_skips = {0, 0};
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void reinitializeI2C() override { m_i2c.initialize(); }

    // Read-only helper for bring-up: sensor model id (0x0219 when the I2C link works).
    uint16_t readChipId() { return m_i2c.read16(DEV, REG_CHIP_ID); }

private:
    static MatrixDimensions kDefaultSize() { return {1080, 1920}; }  // {row, col}

    static constexpr int DEV = 0x10;  // IMX219 7-bit I2C address (datasheet fig. 10)
    static constexpr uint16_t CHIP_ID = 0x0219;
    static constexpr double kDefaultExposureMs = 10.0;

    static constexpr uint16_t REG_CHIP_ID = 0x0000;
    static constexpr uint16_t REG_MODE_SELECT = 0x0100;
    static constexpr uint16_t REG_CSI_LANE_MODE = 0x0114;
    static constexpr uint16_t REG_DPHY_CTRL = 0x0128;
    static constexpr uint16_t REG_EXCK_FREQ = 0x012a;
    static constexpr uint16_t REG_ANALOG_GAIN = 0x0157;
    static constexpr uint16_t REG_DIGITAL_GAIN = 0x0158;
    static constexpr uint16_t REG_COARSE_INTEGRATION_TIME = 0x015a;
    static constexpr uint16_t REG_FRM_LENGTH_A = 0x0160;
    static constexpr uint16_t REG_LINE_LENGTH_A = 0x0162;
    static constexpr uint16_t REG_X_ADD_STA_A = 0x0164;
    static constexpr uint16_t REG_X_ADD_END_A = 0x0166;
    static constexpr uint16_t REG_Y_ADD_STA_A = 0x0168;
    static constexpr uint16_t REG_Y_ADD_END_A = 0x016a;
    static constexpr uint16_t REG_X_OUTPUT_SIZE = 0x016c;
    static constexpr uint16_t REG_Y_OUTPUT_SIZE = 0x016e;
    static constexpr uint16_t REG_X_ODD_INC_A = 0x0170;
    static constexpr uint16_t REG_Y_ODD_INC_A = 0x0171;
    static constexpr uint16_t REG_ORIENTATION = 0x0172;
    static constexpr uint16_t REG_BINNING_MODE_H = 0x0174;
    static constexpr uint16_t REG_BINNING_MODE_V = 0x0175;
    static constexpr uint16_t REG_CSI_DATA_FORMAT_A = 0x018c;
    static constexpr uint16_t REG_VTPXCK_DIV = 0x0301;
    static constexpr uint16_t REG_VTSYCK_DIV = 0x0303;
    static constexpr uint16_t REG_PREPLLCK_VT_DIV = 0x0304;
    static constexpr uint16_t REG_PREPLLCK_OP_DIV = 0x0305;
    static constexpr uint16_t REG_PLL_VT_MPY = 0x0306;
    static constexpr uint16_t REG_OPPXCK_DIV = 0x0309;
    static constexpr uint16_t REG_OPSYCK_DIV = 0x030b;
    static constexpr uint16_t REG_PLL_OP_MPY = 0x030c;
    static constexpr uint16_t REG_TP_WINDOW_WIDTH = 0x0624;
    static constexpr uint16_t REG_TP_WINDOW_HEIGHT = 0x0626;

    static constexpr double PIXEL_RATE_HZ = 182400000.0;
    static constexpr int LINE_LENGTH_PCK = 3448;     // 0x0d78 (minimum, kernel default)
    static constexpr int FRAME_LENGTH_LINES = 1763;  // 30 fps at 1920x1080
    static constexpr int EXPOSURE_MIN_LINES = 4;

    void w8(uint16_t reg, uint8_t data) { m_i2c.write8(DEV, reg, data); }
    void w16(uint16_t reg, uint16_t data) { m_i2c.write16(DEV, reg, data); }  // one 2-byte burst

    // 1920x1080 30 fps, RAW10, no binning. Crop is centred in the 3296x2480 native array
    // (active area starts at (8,8)): left=688, top=700 -> start (680,692).
    void setupMode1080p() {
        w16(REG_X_ADD_STA_A, 680);
        w16(REG_X_ADD_END_A, 680 + 1920 - 1);
        w16(REG_Y_ADD_STA_A, 692);
        w16(REG_Y_ADD_END_A, 692 + 1080 - 1);
        w8(REG_BINNING_MODE_H, 0x00);
        w8(REG_BINNING_MODE_V, 0x00);
        w16(REG_X_OUTPUT_SIZE, 1920);
        w16(REG_Y_OUTPUT_SIZE, 1080);
        w16(REG_TP_WINDOW_WIDTH, 1920);
        w16(REG_TP_WINDOW_HEIGHT, 1080);
        w16(REG_CSI_DATA_FORMAT_A, (10 << 8) | 10);  // RAW10
        w8(REG_OPPXCK_DIV, 10);
        w16(REG_LINE_LENGTH_A, LINE_LENGTH_PCK);
        w16(REG_FRM_LENGTH_A, FRAME_LENGTH_LINES);
    }

    I2CController m_i2c;
    MatrixDimensions m_size;
    MatrixDimensions m_skips;
    MatrixDimensions m_frameDimensions;
    CameraExposure m_exposure = 0;
};

}  // namespace okcli
