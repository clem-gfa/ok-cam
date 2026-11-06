/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// IISP.h — Image Signal Processor interface.
//
// Configuration (setGains/setAWBThreshold) is safe to call any time. Lifecycle
// (initialize/start/stop) is driven by the capture sequencer. Two implementations:
// ISPDriver (real HLS ISP IP) and NullISPDriver (nocam bitfiles, which omit the ISP).

#pragma once

#include <cstdint>

namespace okcli {

class IISP {
public:
    virtual ~IISP() = default;

    virtual uint32_t rgain() const = 0;
    virtual uint32_t ggain() const = 0;
    virtual uint32_t bgain() const = 0;
    virtual uint32_t awb() const = 0;

    // Configuration — safe to call at any time.
    virtual void setGains(uint32_t rgain, uint32_t ggain, uint32_t bgain) = 0;
    virtual void setAWBThreshold(uint32_t awb) = 0;

    // Lifecycle — called by the sequencer during pipeline sequencing.
    virtual void initialize(uint32_t width, uint32_t height, uint32_t awbThresh,
                            uint32_t rgain, uint32_t ggain, uint32_t bgain) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

}  // namespace okcli
