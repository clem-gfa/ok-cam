/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// ITPG.h — Video Test Pattern Generator interface.

#pragma once

#include <cstdint>

namespace okcli {

class ITPG {
public:
    virtual ~ITPG() = default;

    virtual uint32_t patternId() const = 0;
    virtual uint32_t motionSpeed() const = 0;

    // Configuration — safe to call at any time.
    virtual void setPattern(uint32_t patternId) = 0;
    virtual void setMotionSpeed(uint32_t speed) = 0;

    // Lifecycle — called by the sequencer during pipeline sequencing.
    virtual void setResolution(uint32_t width, uint32_t height) = 0;
    virtual void start(bool enableInput) = 0;
    virtual void stop() = 0;
};

}  // namespace okcli
