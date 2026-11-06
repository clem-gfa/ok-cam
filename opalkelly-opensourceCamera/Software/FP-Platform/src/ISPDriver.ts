/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IAxiLite } from "./IAxi";
import { IISP } from "./IISP";

// ============================================================
// ISP REGISTER OFFSETS (base address 0x4CE00000)
// ============================================================
const ISP_BASE = 0x4ce00000;
const ISP_CTRL_REG = 0x00;
const ISP_HEIGHT_REG = 0x10;
const ISP_WIDTH_REG = 0x18;
const ISP_RGAIN_REG = 0x20;
const ISP_GGAIN_REG = 0x28;
const ISP_BGAIN_REG = 0x30;
const ISP_AWB_THRESH_REG = 0x38;

// Control register bits
const ISP_START = 1 << 0;
const ISP_AUTO_RESTART = 1 << 7;

/**
 * Image Signal Processor driver.
 *
 * Register-access layer for the ISP IP with state caching.
 * Manages color gains, AWB threshold, frame dimensions, and start/stop.
 */
export class ISPDriver implements IISP {
    private readonly _axiLite: IAxiLite;

    // Cached configuration state (re-applied during pipeline reconfiguration)
    private _width: number = 0;
    private _height: number = 0;
    private _rgain: number = 128;
    private _ggain: number = 128;
    private _bgain: number = 128;
    private _awb: number = 255;

    public get width(): number { return this._width; }
    public get height(): number { return this._height; }
    public get rgain(): number { return this._rgain; }
    public get ggain(): number { return this._ggain; }
    public get bgain(): number { return this._bgain; }
    public get awb(): number { return this._awb; }

    constructor(axiLite: IAxiLite) {
        this._axiLite = axiLite;
    }

    /** Configure all ISP parameters (gains, AWB threshold, and frame dimensions). */
    async initialize(
        width: number,
        height: number,
        awbThresh: number,
        rgain: number,
        ggain: number,
        bgain: number
    ): Promise<void> {
        this._width = width;
        this._height = height;
        this._awb = awbThresh;
        this._rgain = rgain;
        this._ggain = ggain;
        this._bgain = bgain;

        await this._axiLite.write32(ISP_BASE + ISP_AWB_THRESH_REG, awbThresh);
        await this._axiLite.write32(ISP_BASE + ISP_RGAIN_REG, rgain);
        await this._axiLite.write32(ISP_BASE + ISP_GGAIN_REG, ggain);
        await this._axiLite.write32(ISP_BASE + ISP_BGAIN_REG, bgain);
        await this._axiLite.write32(ISP_BASE + ISP_HEIGHT_REG, height);
        await this._axiLite.write32(ISP_BASE + ISP_WIDTH_REG, width);
    }

    /** Update RGB color gains while the pipeline is running. */
    async setGains(rgain: number, ggain: number, bgain: number): Promise<void> {
        this._rgain = rgain;
        this._ggain = ggain;
        this._bgain = bgain;

        await this._axiLite.write32(ISP_BASE + ISP_RGAIN_REG, rgain);
        await this._axiLite.write32(ISP_BASE + ISP_GGAIN_REG, ggain);
        await this._axiLite.write32(ISP_BASE + ISP_BGAIN_REG, bgain);
    }

    /** Update the AWB threshold while the pipeline is running. */
    async setAWBThreshold(awb: number): Promise<void> {
        this._awb = awb;

        await this._axiLite.write32(ISP_BASE + ISP_AWB_THRESH_REG, awb);
    }

    /** Start the ISP with auto-restart enabled. */
    async start(): Promise<void> {
        let ctrl = await this._axiLite.read32(ISP_BASE + ISP_CTRL_REG);
        ctrl |= ISP_START | ISP_AUTO_RESTART;
        await this._axiLite.write32(ISP_BASE + ISP_CTRL_REG, ctrl);
    }

    /** Stop the ISP by clearing the start and auto-restart bits. */
    async stop(): Promise<void> {
        let ctrl = await this._axiLite.read32(ISP_BASE + ISP_CTRL_REG);
        ctrl &= ~(ISP_START | ISP_AUTO_RESTART);
        await this._axiLite.write32(ISP_BASE + ISP_CTRL_REG, ctrl);
    }
}
