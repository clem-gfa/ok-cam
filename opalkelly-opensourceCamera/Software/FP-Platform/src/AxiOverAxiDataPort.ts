/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import {
    DataBuffer,
    IFPGADataPortAXI,
    IFPGADataPortAXILite,
    IFPGADataPortAXIStream
} from "@opalkelly/frontpanel-platform-api";

import { IAxiLite, IAxiStream } from "./IAxi";

// Default timeout for AXI-Lite operations (ms)
const AXI_TIMEOUT_MS = 5000;

/**
 * AXI-Lite interface backed by a native AXI data port.
 *
 * Handles BigInt conversion and datapath-width buffer padding so that
 * consumers (sub-drivers, I2CController) can work with plain numbers.
 */
export class AxiLiteOverAxiDataPort implements IAxiLite {
    private readonly _axiLite: IFPGADataPortAXILite;
    private readonly _axiStream: IFPGADataPortAXIStream;

    constructor(port: IFPGADataPortAXI) {
        this._axiLite = port.axiLite;
        this._axiStream = port.axiStream;
    }

    async read32(address: number): Promise<number> {
        return await this._axiLite.read(address, AXI_TIMEOUT_MS);
    }

    async write32(address: number, value: number): Promise<void> {
        await this._axiLite.write(address, value >>> 0, AXI_TIMEOUT_MS);
    }

    async resetSystem(): Promise<void> {
        await this._axiStream.reset();
    }
}

/**
 * AXI-Stream interface backed by a native AXI data port.
 */
export class AxiStreamOverAxiDataPort implements IAxiStream {
    private readonly _axiStream: IFPGADataPortAXIStream;

    constructor(port: IFPGADataPortAXI) {
        this._axiStream = port.axiStream;
    }

    async read(buffer: DataBuffer, timeoutMs: number): Promise<void> {
        await this._axiStream.read(buffer, timeoutMs);
    }
}
