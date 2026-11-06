/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IAxiLite } from "./IAxi";

// ============================================================
// HISTOGRAM REGISTER OFFSETS (base address 0x51000000)
// ============================================================
const HIST_BASE = 0x51000000;
const HIST_CTRL_REG = 0x00;
const HIST_ROWS_REG = 0x10;
const HIST_COLS_REG = 0x18;

// Control register bits
const HIST_START = 1 << 0;
const HIST_AUTO_RESTART = 1 << 7;

/**
 * Histogram accelerator driver.
 *
 * Stateless register-access layer for the Histogram IP.
 * Manages input dimensions and start/stop.
 */
export class HistogramDriver {
    private readonly _axiLite: IAxiLite;

    constructor(axiLite: IAxiLite) {
        this._axiLite = axiLite;
    }

    /** Configure the histogram input dimensions. */
    async initialize(rows: number, cols: number): Promise<void> {
        await this._axiLite.write32(HIST_BASE + HIST_ROWS_REG, rows);
        await this._axiLite.write32(HIST_BASE + HIST_COLS_REG, cols);
    }

    /** Start the histogram accelerator with auto-restart enabled. */
    async start(): Promise<void> {
        let ctrl = await this._axiLite.read32(HIST_BASE + HIST_CTRL_REG);
        ctrl |= HIST_START | HIST_AUTO_RESTART;
        await this._axiLite.write32(HIST_BASE + HIST_CTRL_REG, ctrl);
    }

    /** Stop the histogram accelerator by clearing the start and auto-restart bits. */
    async stop(): Promise<void> {
        let ctrl = await this._axiLite.read32(HIST_BASE + HIST_CTRL_REG);
        ctrl &= ~(HIST_START | HIST_AUTO_RESTART);
        await this._axiLite.write32(HIST_BASE + HIST_CTRL_REG, ctrl);
    }
}
