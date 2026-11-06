/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IAxiLite } from "./IAxi";

// ============================================================
// STREAM SWITCH REGISTER OFFSETS (base address 0x55200000)
// ============================================================
const STREAM_SWITCH_BASE = 0x55200000;
const SWITCH_CTRL_REG = 0x00;
const SWITCH_MI0_MUX_REG = 0x40;
const SWITCH_CTRL_UPDATE_BIT = 1 << 1;

/**
 * AXI4-Stream Switch driver.
 *
 * Stateless register-access layer for the AMD AXI4-Stream Switch IP
 * configured in ROUTING_MODE=1. Routes one of multiple slave inputs
 * (video or histogram) to the single master output.
 *
 * After reset, all routes are disabled (MI0_MUX=0x80000000). The caller
 * must explicitly configure the switch before reading data.
 */
export class StreamSwitchDriver {
    private readonly _axiLite: IAxiLite;

    constructor(axiLite: IAxiLite) {
        this._axiLite = axiLite;
    }

    /**
     * Route the specified slave input to the master output.
     * @param slaveIndex - 0 for video (SI0), 1 for histogram (SI1).
     */
    async setSlave(slaveIndex: number): Promise<void> {
        await this._axiLite.write32(STREAM_SWITCH_BASE + SWITCH_MI0_MUX_REG, slaveIndex);
        await this._axiLite.write32(STREAM_SWITCH_BASE + SWITCH_CTRL_REG, SWITCH_CTRL_UPDATE_BIT);
    }
}
