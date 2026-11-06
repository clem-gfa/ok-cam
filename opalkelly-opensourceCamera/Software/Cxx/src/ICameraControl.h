/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// ICameraControl.h — camera sensor control interface.
//
// Camera controls are capability bags: they expose what they can do (I2C registers, exposure,
// resolution/skips) but know nothing about pipeline sequencing. The CapturePipelineSequencer
// calls these at the right moments. Pipeline IP config (ISP gains, TPG patterns) goes through
// IISP / ITPG instead.

#pragma once

#include <vector>

#include "CameraTypes.h"

namespace okcli {

class ICameraControl {
public:
    virtual ~ICameraControl() = default;

    // Accessors.
    virtual MatrixDimensions defaultSize() const = 0;
    virtual std::vector<MatrixDimensions> supportedSkips() const = 0;
    virtual std::vector<TestMode> supportedTestModes() const = 0;
    virtual std::vector<FrameConfiguration> supportedFrameConfigurations() const = 0;
    virtual MatrixDimensions frameDimensions() const = 0;
    virtual CameraExposure exposure() const = 0;

    // Sensor setup only (no pipeline reset).
    virtual void initialize() = 0;

    // Sensor configuration.
    virtual void setExposure(CameraExposure exposure) = 0;
    virtual void setSize(const MatrixDimensions& dimensions) = 0;
    virtual void setSkips(const MatrixDimensions& dimensions) = 0;

    // Called by the sequencer after a system reset to re-init the AXI IIC IP.
    virtual void reinitializeI2C() = 0;
};

}  // namespace okcli
