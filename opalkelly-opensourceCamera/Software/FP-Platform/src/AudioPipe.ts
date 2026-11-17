/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IFPGADataPortClassic } from "@opalkelly/frontpanel-platform-api";

import { AUDIO_BLOCK_SIZE, AUDIO_PIPE_ADDRESS, IAudioPipe } from "./AudioStream";

// Endpoints of the microphone gateware (Gateware/XEM8310-AU25P/Microphones, mic_subsystem.v)
export const MIC_WI_CONTROL = 0x01; // bit0 enable, bit1 reset, bits[15:8] sampling delay (clk cycles)
export const MIC_WO_STATUS = 0x20; // bit0 run, bit1 ultrasonic clock, bit2 present/locked, bit3 FIFO was full
export const MIC_WO_DROPPED = 0x21;
export const MIC_WO_OVERRUNS = 0x22;
export const MIC_WO_LEVEL = 0x23; // 32-bit words waiting in the audio FIFO
export const MIC_WO_RING = 0x24; // {peak[31:16], current[15:0]} packets in the DDR4 ring buffer (0 without the DDR4 ring)

export const MIC_STATUS_RUN = 0x1;
export const MIC_STATUS_PRESENT = 0x4;

/** IAudioPipe over block pipe 0xA1 of the Classic data port. Call only on the device queue. */
export class AudioPipeOverClassicDataPort implements IAudioPipe {
    private readonly _dataPort: IFPGADataPortClassic;

    constructor(dataPort: IFPGADataPortClassic) {
        this._dataPort = dataPort;
    }

    /** DDR4 ring occupancy read together with the FIFO level (same USB transaction). */
    ringUsed = 0;
    ringPeak = 0;

    async available(): Promise<number> {
        await this._dataPort.updateWireOuts();
        const ring = this._dataPort.getWireOutValue(MIC_WO_RING) >>> 0;
        this.ringUsed = ring & 0xffff;
        this.ringPeak = (ring >>> 16) & 0xffff;
        return this._dataPort.getWireOutValue(MIC_WO_LEVEL) >>> 0;
    }

    async read(buffer: Uint8Array): Promise<void> {
        let timer: ReturnType<typeof setTimeout> | undefined;
        const timeout = new Promise<never>((_, reject) => {
            timer = setTimeout(() => reject(new Error("audio pipe read timed out (are the microphones running?)")), 2000);
        });
        let got: number;
        try {
            got = await Promise.race([this._dataPort.readFromBlockPipeOut(AUDIO_PIPE_ADDRESS, AUDIO_BLOCK_SIZE, buffer), timeout]);
        } finally {
            clearTimeout(timer);
        }
        if (got !== buffer.byteLength) throw new Error(`audio short read: ${got} of ${buffer.byteLength} bytes`);
    }
}

/**
 * True when the loaded bitfile contains the microphone subsystem. Without it, reading the audio pipe would
 * wait for data that never comes and block the device queue, so this must be checked first.
 */
export async function micPresent(dataPort: IFPGADataPortClassic): Promise<boolean> {
    await dataPort.updateWireOuts();
    return (dataPort.getWireOutValue(MIC_WO_STATUS) & MIC_STATUS_PRESENT) !== 0;
}

export async function micStatus(dataPort: IFPGADataPortClassic): Promise<number> {
    await dataPort.updateWireOuts();
    return dataPort.getWireOutValue(MIC_WO_STATUS) >>> 0;
}

/** Resets the microphone subsystem then enables (or disables) it. */
export async function micSetEnabled(dataPort: IFPGADataPortClassic, enable: boolean, sampleDelay = 7): Promise<void> {
    dataPort.setWireInValue(MIC_WI_CONTROL, 0x2, 0xffffffff); // reset, disabled
    await dataPort.updateWireIns();
    dataPort.setWireInValue(MIC_WI_CONTROL, enable ? ((sampleDelay & 0xff) << 8) | 0x1 : 0x0, 0xffffffff);
    await dataPort.updateWireIns();
}

export interface MicCounters {
    status: number;
    dropped: number; // packets the gateware dropped because its FIFO was full (the PC did not read fast enough)
    overruns: number;
    level: number; // words waiting in the audio FIFO
    ringUsed: number; // packets currently held in the DDR4 ring (gateware built with the DDR4 ring)
    ringPeak: number; // maximum since the last reset: how deep the PC backlog got
}

export async function micCounters(dataPort: IFPGADataPortClassic): Promise<MicCounters> {
    await dataPort.updateWireOuts();
    return {
        status: dataPort.getWireOutValue(MIC_WO_STATUS) >>> 0,
        dropped: dataPort.getWireOutValue(MIC_WO_DROPPED) >>> 0,
        overruns: dataPort.getWireOutValue(MIC_WO_OVERRUNS) >>> 0,
        level: dataPort.getWireOutValue(MIC_WO_LEVEL) >>> 0,
        ringUsed: dataPort.getWireOutValue(MIC_WO_RING) & 0xffff,
        ringPeak: (dataPort.getWireOutValue(MIC_WO_RING) >>> 16) & 0xffff,
    };
}
