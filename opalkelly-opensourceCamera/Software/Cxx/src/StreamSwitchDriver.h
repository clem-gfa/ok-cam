/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// StreamSwitchDriver.h — AXI4-Stream Switch driver.
//
// Stateless register-access layer for the AMD AXI4-Stream Switch (ROUTING_MODE=1, base
// 0x55200000). Routes one slave input (video or histogram) to the single master output.
// After reset all routes are disabled; the caller must set the slave before every read.

#pragma once

#include <cstdint>

#include "Axi.h"

namespace okcli {

class StreamSwitchDriver {
public:
    explicit StreamSwitchDriver(IAxiLite& axiLite) : m_axi(axiLite) {}

    // Route the given slave to the master output. 0 = video (SI0), 1 = histogram (SI1).
    void setSlave(uint32_t slaveIndex) {
        m_axi.write32(STREAM_SWITCH_BASE + SWITCH_MI0_MUX_REG, slaveIndex);
        m_axi.write32(STREAM_SWITCH_BASE + SWITCH_CTRL_REG, SWITCH_CTRL_UPDATE_BIT);
    }

private:
    static constexpr uint64_t STREAM_SWITCH_BASE   = 0x55200000ull;
    static constexpr uint64_t SWITCH_CTRL_REG      = 0x00;
    static constexpr uint64_t SWITCH_MI0_MUX_REG   = 0x40;
    static constexpr uint32_t SWITCH_CTRL_UPDATE_BIT = 1u << 1;

    IAxiLite& m_axi;
};

}  // namespace okcli
