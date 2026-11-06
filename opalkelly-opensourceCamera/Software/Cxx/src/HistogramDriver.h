/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// HistogramDriver.h — Histogram accelerator driver.
//
// Stateless register-access layer for the Histogram IP (base 0x51000000).

#pragma once

#include <cstdint>

#include "Axi.h"

namespace okcli {

class HistogramDriver {
public:
    explicit HistogramDriver(IAxiLite& axiLite) : m_axi(axiLite) {}

    // Configure the histogram input dimensions.
    void initialize(uint32_t rows, uint32_t cols) {
        m_axi.write32(HIST_BASE + HIST_ROWS_REG, rows);
        m_axi.write32(HIST_BASE + HIST_COLS_REG, cols);
    }

    void start() {
        uint32_t ctrl = m_axi.read32(HIST_BASE + HIST_CTRL_REG);
        ctrl |= HIST_START | HIST_AUTO_RESTART;
        m_axi.write32(HIST_BASE + HIST_CTRL_REG, ctrl);
    }

    void stop() {
        uint32_t ctrl = m_axi.read32(HIST_BASE + HIST_CTRL_REG);
        ctrl &= ~(HIST_START | HIST_AUTO_RESTART);
        m_axi.write32(HIST_BASE + HIST_CTRL_REG, ctrl);
    }

private:
    static constexpr uint64_t HIST_BASE         = 0x51000000ull;
    static constexpr uint64_t HIST_CTRL_REG     = 0x00;
    static constexpr uint64_t HIST_ROWS_REG     = 0x10;
    static constexpr uint64_t HIST_COLS_REG     = 0x18;
    static constexpr uint32_t HIST_START        = 1u << 0;
    static constexpr uint32_t HIST_AUTO_RESTART = 1u << 7;

    IAxiLite& m_axi;
};

}  // namespace okcli
