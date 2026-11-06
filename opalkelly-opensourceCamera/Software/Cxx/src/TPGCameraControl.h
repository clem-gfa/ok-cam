/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// TPGCameraControl.h — TPG-only control (no physical sensor).

#pragma once

#include <vector>

#include "CameraTypes.h"
#include "ICameraControl.h"
#include "TPGPatterns.h"

namespace okcli {

class TPGCameraControl : public ICameraControl {
public:
    TPGCameraControl()
        : m_size{1080, 1920},  // {rowCount, columnCount} = 1920x1080
          m_skips{0, 0},
          m_frameDimensions(calculateFrameDimensions(m_size, m_skips)) {}

    MatrixDimensions defaultSize() const override { return {1080, 1920}; }

    std::vector<MatrixDimensions> supportedSkips() const override { return {{0, 0}}; }

    std::vector<TestMode> supportedTestModes() const override { return tpgSupportedTestModes(); }

    std::vector<FrameConfiguration> supportedFrameConfigurations() const override {
        std::vector<FrameConfiguration> out;
        for (const MatrixDimensions& d : {MatrixDimensions{1080, 1920}, MatrixDimensions{720, 1280},
                                          MatrixDimensions{480, 640}}) {
            out.push_back({d, {0, 0}});
        }
        return out;
    }

    MatrixDimensions frameDimensions() const override { return m_frameDimensions; }
    CameraExposure exposure() const override { return 0; }

    void initialize() override {}  // TPG generates video internally — no sensor I2C.

    void setExposure(CameraExposure /*exposure*/) override {}  // no sensor

    void setSize(const MatrixDimensions& size) override {
        m_size = size;
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void setSkips(const MatrixDimensions& /*skips*/) override {
        m_skips = {0, 0};
        m_frameDimensions = calculateFrameDimensions(m_size, m_skips);
    }

    void reinitializeI2C() override {}  // no I2C

private:
    MatrixDimensions m_size;
    MatrixDimensions m_skips;
    MatrixDimensions m_frameDimensions;
};

}  // namespace okcli
