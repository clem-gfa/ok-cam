/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IAxiLite } from "./IAxi";
import { ITPG } from "./ITPG";

// ============================================================
// TPG REGISTER OFFSETS (base address 0x59400000)
// ============================================================
const TPG_BASE = 0x59400000;
const TPG_CONTROL_REG = 0x00;
const TPG_ACTIVE_HEIGHT_REG = 0x10;
const TPG_ACTIVE_WIDTH_REG = 0x18;
const TPG_BG_PATTERN_ID_REG = 0x20;
const TPG_MOTION_SPEED_REG = 0x38;
const TPG_ENABLE_INPUT_REG = 0x98;

// Control register bits
const TPG_START = 1 << 0;
const TPG_AUTO_RESTART = 1 << 7;

/**
 * Video Test Pattern Generator driver.
 *
 * Register-access layer for the AMD Video TPG IP with state caching.
 * Manages resolution, pattern selection, motion speed, and start/stop.
 */
export class TPGDriver implements ITPG {
    private readonly _axiLite: IAxiLite;

    // Cached configuration state (re-applied during pipeline reconfiguration)
    private _width: number = 0;
    private _height: number = 0;
    private _patternId: number = 0x00;
    private _motionSpeed: number = 0x03;

    public get width(): number { return this._width; }
    public get height(): number { return this._height; }
    public get patternId(): number { return this._patternId; }
    public get motionSpeed(): number { return this._motionSpeed; }

    constructor(axiLite: IAxiLite) {
        this._axiLite = axiLite;
    }

    /** Set the TPG output resolution. */
    async setResolution(width: number, height: number): Promise<void> {
        this._width = width;
        this._height = height;

        await this._axiLite.write32(TPG_BASE + TPG_ACTIVE_WIDTH_REG, width);
        await this._axiLite.write32(TPG_BASE + TPG_ACTIVE_HEIGHT_REG, height);
    }

    /** Set the background pattern. */
    async setPattern(patternId: number): Promise<void> {
        this._patternId = patternId;

        await this._axiLite.write32(TPG_BASE + TPG_BG_PATTERN_ID_REG, patternId);
    }

    /** Update the motion speed without changing the pattern. */
    async setMotionSpeed(speed: number): Promise<void> {
        this._motionSpeed = speed;

        await this._axiLite.write32(TPG_BASE + TPG_MOTION_SPEED_REG, speed);
    }

    /**
     * Start the TPG with auto-restart enabled.
     * @param enableInput - true to pass through sensor input, false for TPG-only output.
     */
    async start(enableInput: boolean): Promise<void> {
        let ctrl = await this._axiLite.read32(TPG_BASE + TPG_CONTROL_REG);
        ctrl |= TPG_START | TPG_AUTO_RESTART;
        await this._axiLite.write32(TPG_BASE + TPG_CONTROL_REG, ctrl);
        await this._axiLite.write32(TPG_BASE + TPG_ENABLE_INPUT_REG, enableInput ? 1 : 0);
    }

    /** Stop the TPG by clearing the start and auto-restart bits. */
    async stop(): Promise<void> {
        let ctrl = await this._axiLite.read32(TPG_BASE + TPG_CONTROL_REG);
        ctrl &= ~(TPG_START | TPG_AUTO_RESTART);
        await this._axiLite.write32(TPG_BASE + TPG_CONTROL_REG, ctrl);
    }
}
