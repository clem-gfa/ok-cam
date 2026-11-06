/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IAxiLite } from "./IAxi";
import { sleep } from "./Utilities";

// ============================================================
// AXI IIC REGISTER OFFSETS
// ============================================================
const IIC_BASE = 0x40800000;
const XIIC_RESETR_OFFSET = 0x40;
const XIIC_CR_REG_OFFSET = 0x100;
const XIIC_SR_REG_OFFSET = 0x104;
const XIIC_DTR_REG_OFFSET = 0x108;
const XIIC_DRR_REG_OFFSET = 0x10c;
const XIIC_RFD_REG_OFFSET = 0x120;

// AXI IIC bit masks
const XIIC_RESET_MASK = 0x0a;
const XIIC_CR_ENABLE_DEVICE_MASK = 0x01;
const XIIC_CR_TX_FIFO_RESET_MASK = 0x02;
const XIIC_SR_BUS_BUSY_MASK = 0x04;
const XIIC_SR_RX_FIFO_EMPTY_MASK = 0x40;
const XIIC_SR_TX_FIFO_EMPTY_MASK = 0x80;
const XIIC_TX_DYN_START_MASK = 0x100;
const XIIC_TX_DYN_STOP_MASK = 0x200;
const IIC_RX_FIFO_DEPTH = 16;

/**
 * Standalone I2C controller using the AMD AXI IIC IP in dynamic mode.
 *
 * Extracted from AXICameraDriver so that both AXI and Classic
 * paths can share the same I2C implementation via IAxiLite.
 */
export class I2CController {
    private readonly _axiLite: IAxiLite;

    constructor(axiLite: IAxiLite) {
        this._axiLite = axiLite;
    }

    /** Initialize the AXI IIC IP in dynamic mode. Idempotent (starts with soft-reset). */
    async initialize(): Promise<void> {
        await this._axiLite.write32(IIC_BASE + XIIC_RESETR_OFFSET, XIIC_RESET_MASK);
        await this._axiLite.write32(IIC_BASE + XIIC_RFD_REG_OFFSET, IIC_RX_FIFO_DEPTH - 1);
        await this._axiLite.write32(IIC_BASE + XIIC_CR_REG_OFFSET, XIIC_CR_TX_FIFO_RESET_MASK);
        await this._axiLite.write32(IIC_BASE + XIIC_CR_REG_OFFSET, XIIC_CR_ENABLE_DEVICE_MASK);

        const st = await this._axiLite.read32(IIC_BASE + XIIC_SR_REG_OFFSET);
        const expected = XIIC_SR_RX_FIFO_EMPTY_MASK | XIIC_SR_TX_FIFO_EMPTY_MASK;

        if ((st & expected) !== expected) {
            throw new Error(`AXI IIC dynamic init failed. Status=0x${st.toString(16)}`);
        }

        console.log("AXI IIC dynamic mode initialized.");
    }

    // ================================================================
    // Public: 16-bit register address, 16-bit data (AR0330)
    // ================================================================

    async read16(deviceAddress: number, registerAddress: number): Promise<number> {
        if (!(await this.waitBusFree())) {
            throw new Error("I2C bus never became free before read16()");
        }

        // Send register address (no stop — repeated start)
        const addrBytes = [(registerAddress >> 8) & 0xff, registerAddress & 0xff];
        await this.dynSend(deviceAddress, addrBytes, false);

        // Receive 2 data bytes
        const rx = await this.dynRecv(deviceAddress, 2);

        if (!(await this.waitBusFree())) {
            throw new Error("I2C bus did not free after read16()");
        }

        return (rx[0] << 8) | rx[1];
    }

    async write16(
        deviceAddress: number,
        registerAddress: number,
        data: number
    ): Promise<void> {
        if (!(await this.waitBusFree())) {
            throw new Error("I2C bus never became free before write16()");
        }

        const bytesToSend = [
            (registerAddress >> 8) & 0xff,
            registerAddress & 0xff,
            (data >> 8) & 0xff,
            data & 0xff
        ];

        await this.dynSend(deviceAddress, bytesToSend, true);
    }

    // ================================================================
    // Public: 16-bit register address, 8-bit data (OV5640/PCAM)
    // ================================================================

    async read8(dev7bit: number, registerAddress: number): Promise<number> {
        if (!(await this.waitBusFree())) {
            throw new Error("I2C bus never became free before read8()");
        }

        // Send register address (no stop — repeated start)
        const addrBytes = [(registerAddress >> 8) & 0xff, registerAddress & 0xff];
        await this.dynSend(dev7bit, addrBytes, false);

        // Receive 1 data byte
        const rx = await this.dynRecv(dev7bit, 1);

        if (!(await this.waitBusFree())) {
            throw new Error("I2C bus did not free after read8()");
        }

        return rx[0];
    }

    async write8(dev7bit: number, registerAddress: number, data: number): Promise<void> {
        if (!(await this.waitBusFree())) {
            throw new Error("I2C bus never became free before write8()");
        }

        const bytesToSend = [
            (registerAddress >> 8) & 0xff,
            registerAddress & 0xff,
            data & 0xff
        ];

        await this.dynSend(dev7bit, bytesToSend, true);
    }

    // ================================================================
    // Private: I2C Dynamic Mode Helpers
    // ================================================================

    private async waitBusFree(timeoutMs: number = 1000): Promise<boolean> {
        const startTime = performance.now();

        while (true) {
            const sr = await this._axiLite.read32(IIC_BASE + XIIC_SR_REG_OFFSET);

            if ((sr & XIIC_SR_BUS_BUSY_MASK) === 0) {
                return true;
            }

            if (performance.now() - startTime > timeoutMs) {
                return false;
            }

            await sleep(1);
        }
    }

    private async dynSend(
        dev7bit: number,
        byteList: number[],
        sendStop: boolean = true
    ): Promise<void> {
        const addrByte = (dev7bit << 1) | 0;
        await this._axiLite.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_START_MASK | addrByte);

        for (let i = 0; i < byteList.length; i++) {
            if (i === byteList.length - 1 && sendStop) {
                await this._axiLite.write32(
                    IIC_BASE + XIIC_DTR_REG_OFFSET,
                    XIIC_TX_DYN_STOP_MASK | byteList[i]
                );
            } else {
                await this._axiLite.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, byteList[i]);
            }
        }
    }

    private async dynRecv(dev7bit: number, count: number, timeoutMs: number = 1000): Promise<number[]> {
        if (count <= 0 || count > 255) {
            throw new Error("I2C recv byte count must be 1..255");
        }

        const addrByte = (dev7bit << 1) | 1;
        await this._axiLite.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_START_MASK | addrByte);
        await this._axiLite.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_STOP_MASK | count);

        const result: number[] = [];
        const startTime = performance.now();

        while (result.length < count) {
            if (performance.now() - startTime > timeoutMs) {
                throw new Error(`I2C recv timed out after ${timeoutMs}ms (received ${result.length}/${count} bytes)`);
            }

            const sr = await this._axiLite.read32(IIC_BASE + XIIC_SR_REG_OFFSET);

            if ((sr & XIIC_SR_RX_FIFO_EMPTY_MASK) === 0) {
                const b = await this._axiLite.read32(IIC_BASE + XIIC_DRR_REG_OFFSET);
                result.push(b & 0xff);
            } else {
                await sleep(1);
            }
        }

        return result;
    }
}
