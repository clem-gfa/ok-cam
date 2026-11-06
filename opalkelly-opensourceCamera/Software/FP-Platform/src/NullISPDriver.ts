/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IISP } from "./IISP";

/**
 * Stub ISP driver used with the No-Camera bitfile variants
 * (camera_xem8320_au25p_nocam.bit and camera_hub1450_au10p_nocam.bit),
 * which omit the HLS ISP IP core entirely. No sensor is connected
 * and video comes from the on-chip Test Pattern Generator, so there
 * is no image signal to process and nothing for the ISP to do.
 *
 * All methods return without performing any hardware access. Gain
 * and AWB threshold values are held in memory only so that UI code
 * reading them back through the IISP interface sees consistent
 * values.
 */
export class NullISPDriver implements IISP {
    private _rgain: number = 128;
    private _ggain: number = 128;
    private _bgain: number = 128;
    private _awb: number = 255;

    public get rgain(): number { return this._rgain; }
    public get ggain(): number { return this._ggain; }
    public get bgain(): number { return this._bgain; }
    public get awb(): number { return this._awb; }

    async setGains(rgain: number, ggain: number, bgain: number): Promise<void> {
        this._rgain = rgain;
        this._ggain = ggain;
        this._bgain = bgain;
    }

    async setAWBThreshold(awb: number): Promise<void> {
        this._awb = awb;
    }

    async initialize(
        _width: number,
        _height: number,
        awbThresh: number,
        rgain: number,
        ggain: number,
        bgain: number
    ): Promise<void> {
        this._awb = awbThresh;
        this._rgain = rgain;
        this._ggain = ggain;
        this._bgain = bgain;
    }

    async start(): Promise<void> {}
    async stop(): Promise<void> {}
}
