/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IAxiLite } from "./IAxi";
import { sleep } from "./Utilities";

// ============================================================
// VDMA REGISTER OFFSETS (base address 0x44A00000)
// ============================================================
const VDMA_BASE = 0x44a00000;

// MM2S (read) channel
const VDMA_MM2S_VDMACR = 0x00;
const VDMA_MM2S_VDMASR = 0x04;
const VDMA_PARKPTR = 0x28;

// S2MM (write) channel
const VDMA_S2MM_VDMACR = 0x30;
const VDMA_S2MM_VDMASR = 0x34;

// MM2S size/stride/start-address
const VDMA_MM2S_VSIZE = 0x50;
const VDMA_MM2S_HSIZE = 0x54;
const VDMA_MM2S_FRMDLY_STRIDE = 0x58;
const VDMA_MM2S_START_ADDR1 = 0x5c;
const VDMA_MM2S_START_ADDR2 = 0x60;
const VDMA_MM2S_START_ADDR3 = 0x64;

// S2MM size/stride/start-address
const VDMA_S2MM_VSIZE = 0xa0;
const VDMA_S2MM_HSIZE = 0xa4;
const VDMA_S2MM_FRMDLY_STRIDE = 0xa8;
const VDMA_S2MM_START_ADDR1 = 0xac;
const VDMA_S2MM_START_ADDR2 = 0xb0;
const VDMA_S2MM_START_ADDR3 = 0xb4;

// VDMA control register bits
const VDMA_CR_RUNSTOP_MASK = 0x01;
const VDMA_CR_RESET_MASK = 0x04;

/**
 * Video DMA driver.
 *
 * Stateless register-access layer for the AMD AXI VDMA IP.
 * Manages S2MM (write) and MM2S (read) channels with triple-buffer support.
 */
export class VideoDMADriver {
    private readonly _axiLite: IAxiLite;

    constructor(axiLite: IAxiLite) {
        this._axiLite = axiLite;
    }

    /** Stop the S2MM (write) channel and poll until halted. */
    async stopWriteChannel(timeoutMs: number = 1000): Promise<boolean> {
        const cr = await this._axiLite.read32(VDMA_BASE + VDMA_S2MM_VDMACR);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_VDMACR, cr & ~VDMA_CR_RUNSTOP_MASK);

        const startTime = performance.now();

        while (performance.now() - startTime < timeoutMs) {
            const sr = await this._axiLite.read32(VDMA_BASE + VDMA_S2MM_VDMASR);

            if (sr & 0x1) {
                return true; // Halted
            }

            await sleep(1);
        }

        console.warn("VDMA S2MM channel did not halt within timeout");
        return false;
    }

    /** Stop the MM2S (read) channel and poll until halted. */
    async stopReadChannel(timeoutMs: number = 1000): Promise<boolean> {
        const cr = await this._axiLite.read32(VDMA_BASE + VDMA_MM2S_VDMACR);
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_VDMACR, cr & ~VDMA_CR_RUNSTOP_MASK);

        const startTime = performance.now();

        while (performance.now() - startTime < timeoutMs) {
            const sr = await this._axiLite.read32(VDMA_BASE + VDMA_MM2S_VDMASR);

            if (sr & 0x1) {
                return true; // Halted
            }

            await sleep(1);
        }

        console.warn("VDMA MM2S channel did not halt within timeout");
        return false;
    }

    /** Soft reset both MM2S and S2MM channels. */
    async softReset(): Promise<void> {
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_VDMACR, VDMA_CR_RESET_MASK);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_VDMACR, VDMA_CR_RESET_MASK);
    }

    /**
     * Configure and start the S2MM (write) channel in triple-buffer mode.
     * Writing VSIZE last triggers the channel to start.
     */
    async startWriteChannel(
        widthBytes: number,
        height: number,
        buf0: number,
        buf1: number,
        buf2: number
    ): Promise<void> {
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_VDMACR, 0x8b);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_HSIZE, widthBytes);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_FRMDLY_STRIDE, widthBytes);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_START_ADDR1, buf0);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_START_ADDR2, buf1);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_START_ADDR3, buf2);
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_VSIZE, height); // Writing VSIZE starts S2MM
    }

    /**
     * Configure and start the MM2S (read) channel in triple-buffer mode.
     * Sets read reference frame to 1 for circular buffering.
     * Writing VSIZE last triggers the channel to start.
     */
    async startReadChannel(
        widthBytes: number,
        height: number,
        buf0: number,
        buf1: number,
        buf2: number
    ): Promise<void> {
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_VDMACR, 0x8b);
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_HSIZE, widthBytes);
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_FRMDLY_STRIDE, widthBytes);
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_START_ADDR1, buf0);
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_START_ADDR2, buf1);
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_START_ADDR3, buf2);

        // Set read reference frame to 1 for circular buffering
        let parkptr = await this._axiLite.read32(VDMA_BASE + VDMA_PARKPTR);
        parkptr = (parkptr & ~0xf) | 0x1;
        await this._axiLite.write32(VDMA_BASE + VDMA_PARKPTR, parkptr);

        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_VSIZE, height); // Writing VSIZE starts MM2S
    }

    /** Clear status bits on both S2MM and MM2S channels. */
    async clearStatus(): Promise<void> {
        await this._axiLite.write32(VDMA_BASE + VDMA_S2MM_VDMASR, 0xffffffff);
        await this._axiLite.write32(VDMA_BASE + VDMA_MM2S_VDMASR, 0xffffffff);
    }

    /** Get the S2MM (write channel) status register. */
    async getWriteChannelStatus(): Promise<number> {
        return this._axiLite.read32(VDMA_BASE + VDMA_S2MM_VDMASR);
    }

    /** Get the MM2S (read channel) status register. */
    async getReadChannelStatus(): Promise<number> {
        return this._axiLite.read32(VDMA_BASE + VDMA_MM2S_VDMASR);
    }
}
