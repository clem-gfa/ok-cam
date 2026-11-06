/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { DataBuffer, IFPGADataPortClassic } from "@opalkelly/frontpanel-platform-api";

import { IAxiLite, IAxiStream } from "./IAxi";
import { sleep } from "./Utilities";

// ============================================================
// FrontPanel-to-AXI-Lite Bridge Endpoint Addresses
// (Reference defaults from HDLComponents/FrontPanelToAxiLiteBridge)
// ============================================================

// WireIn endpoints
const BRIDGE_WI_ADDRESS = 0x1d;
const BRIDGE_WI_DATA = 0x1e;
const BRIDGE_WI_TIMEOUT = 0x1f;

// WireOut endpoints
const BRIDGE_WO_DATA = 0x3e;
const BRIDGE_WO_STATUS = 0x3f;

// TriggerIn endpoint
const BRIDGE_TI_OPERATION = 0x5f;
const BRIDGE_TI_WRITE_BIT = 0;
const BRIDGE_TI_READ_BIT = 1;

// AXI system reset endpoint
const AXI_RESET_WI = 0x00;

// Status register bit masks
const STATUS_BUSY_MASK = 0x01;
const STATUS_RESPONSE_SHIFT = 1;
const STATUS_RESPONSE_MASK = 0x07; // 3 bits

// AXI response codes
const RESPONSE_OKAY = 0b000;
const RESPONSE_SLVERR = 0b010;
const RESPONSE_DECERR = 0b011;
const RESPONSE_HW_TIMEOUT = 0b100;

// Timing constants (from Python reference)
const NS_PER_FRONTPANEL_CLOCK_PERIOD = 9.920;
const MS_TO_NS = 1e6;
const STATUS_CHECK_INTERVAL_MS = 10;
const DEFAULT_HARDWARE_TIMEOUT_MS = 3000;

// Default software timeout for bridge operations (ms)
const DEFAULT_SOFTWARE_TIMEOUT_MS = 5000;

// ============================================================
// BlockPipeOut Configuration
// ============================================================
const STREAM_PIPE_ADDRESS = 0xa0;
const STREAM_BLOCK_SIZE = 1024;

/**
 * AXI-Lite interface backed by Classic FrontPanel endpoints via a
 * FrontPanel-to-AXI-Lite bridge gateware module.
 *
 * Translates 32-bit register reads/writes into WireIn/WireOut/TriggerIn
 * transactions through the fp_to_axil gateware.
 *
 * Ported from the Python reference implementation at:
 * design-resources/HDLComponents/FrontPanelToAxiLiteBridge/python_api/FrontPanelToAxiLiteBridge.py
 */
export class AxiLiteOverClassicDataPort implements IAxiLite {
    private readonly _dataPort: IFPGADataPortClassic;

    constructor(dataPort: IFPGADataPortClassic) {
        this._dataPort = dataPort;

        // Configure the hardware timeout in the bridge gateware
        const timeoutClockPeriods = Math.floor(
            (DEFAULT_HARDWARE_TIMEOUT_MS * MS_TO_NS) / NS_PER_FRONTPANEL_CLOCK_PERIOD
        );
        this._dataPort.setWireInValue(BRIDGE_WI_TIMEOUT, timeoutClockPeriods, 0xFFFFFFFF);
        // Defer UpdateWireIns to the first read/write operation
    }

    async read32(address: number): Promise<number> {
        const addr32 = address >>> 0;
        const startTime = performance.now();

        // Set address and trigger read
        this._dataPort.setWireInValue(BRIDGE_WI_ADDRESS, addr32, 0xFFFFFFFF);
        await this._dataPort.updateWireIns();
        await this._dataPort.activateTriggerIn(BRIDGE_TI_OPERATION, BRIDGE_TI_READ_BIT);

        await this.pollUntilReady("read", addr32, startTime, DEFAULT_SOFTWARE_TIMEOUT_MS);

        // Read data
        return this._dataPort.getWireOutValue(BRIDGE_WO_DATA);
    }

    async write32(address: number, value: number): Promise<void> {
        const addr32 = address >>> 0;
        const value32 = value >>> 0;
        const startTime = performance.now();

        // Set address and data, trigger write
        this._dataPort.setWireInValue(BRIDGE_WI_ADDRESS, addr32, 0xFFFFFFFF);
        this._dataPort.setWireInValue(BRIDGE_WI_DATA, value32, 0xFFFFFFFF);
        await this._dataPort.updateWireIns();
        await this._dataPort.activateTriggerIn(BRIDGE_TI_OPERATION, BRIDGE_TI_WRITE_BIT);

        await this.pollUntilReady("write", addr32, startTime, DEFAULT_SOFTWARE_TIMEOUT_MS);
    }

    async resetSystem(): Promise<void> {
        // Assert AXI system reset via the axi_reset module
        this._dataPort.setWireInValue(AXI_RESET_WI, 1, 0xFFFFFFFF);
        await this._dataPort.updateWireIns();
        this._dataPort.setWireInValue(AXI_RESET_WI, 0, 0xFFFFFFFF);
        await this._dataPort.updateWireIns();
    }

    private async pollUntilReady(
        operation: string,
        address: number,
        startTime: number,
        timeoutMs: number
    ): Promise<void> {
        await this._dataPort.updateWireOuts();
        let rawStatus = this._dataPort.getWireOutValue(BRIDGE_WO_STATUS);

        while ((rawStatus & STATUS_BUSY_MASK) !== 0) {
            const elapsedMs = performance.now() - startTime;
            if (elapsedMs > timeoutMs) {
                throw new Error(
                    `AXI-Lite bridge ${operation} timed out at address 0x${address.toString(16)} ` +
                        `after ${Math.round(elapsedMs)}ms`
                );
            }

            await sleep(STATUS_CHECK_INTERVAL_MS);
            await this._dataPort.updateWireOuts();
            rawStatus = this._dataPort.getWireOutValue(BRIDGE_WO_STATUS);
        }

        const responseBits =
            (rawStatus >> STATUS_RESPONSE_SHIFT) & STATUS_RESPONSE_MASK;
        this.checkResponse(responseBits, operation, address);
    }

    private checkResponse(
        responseBits: number,
        operation: string,
        address: number
    ): void {
        switch (responseBits) {
            case RESPONSE_OKAY:
                return;
            case RESPONSE_SLVERR:
                throw new Error(
                    `AXI-Lite ${operation} SLVERR at address 0x${address.toString(16)}`
                );
            case RESPONSE_DECERR:
                throw new Error(
                    `AXI-Lite ${operation} DECERR (no slave) at address 0x${address.toString(16)}`
                );
            case RESPONSE_HW_TIMEOUT:
                throw new Error(
                    `AXI-Lite ${operation} hardware timeout at address 0x${address.toString(16)}. ` +
                        `Reset AXI system to recover.`
                );
            default:
                throw new Error(
                    `AXI-Lite ${operation} unknown response 0b${responseBits.toString(2)} ` +
                        `at address 0x${address.toString(16)}`
                );
        }
    }
}

/**
 * AXI-Stream interface backed by a Classic FrontPanel BlockPipeOut endpoint.
 *
 * Data is read from the Pipe-to-AXI-Stream shim in the gateware.
 * Since readFromBlockPipeOut has no native timeout, this wraps the
 * read with Promise.race.
 *
 * Note: the underlying USB transfer is not cancelled on timeout —
 * recovery requires resetSystem() to reinitialize the pipe shim.
 */
export class AxiStreamOverClassicDataPort implements IAxiStream {
    private readonly _dataPort: IFPGADataPortClassic;

    constructor(dataPort: IFPGADataPortClassic) {
        this._dataPort = dataPort;
    }

    async read(buffer: DataBuffer, timeoutMs: number): Promise<void> {
        const readPromise = this._dataPort.readFromBlockPipeOut(
            STREAM_PIPE_ADDRESS,
            STREAM_BLOCK_SIZE,
            buffer
        );

        // readFromBlockPipeOut has no timeout parameter, so wrap with
        // Promise.race to match native axiStream.read() timeout behavior.
        let timer: ReturnType<typeof setTimeout>;
        const timeoutPromise = new Promise<never>((_, reject) => {
            timer = setTimeout(
                () =>
                    reject(
                        new Error(
                            `AXI-Stream read timed out after ${timeoutMs}ms ` +
                                `(requested ${buffer.byteLength} bytes)`
                        )
                    ),
                timeoutMs
            );
        });

        try {
            await Promise.race([readPromise, timeoutPromise]);
        } finally {
            clearTimeout(timer!);
        }
    }
}
