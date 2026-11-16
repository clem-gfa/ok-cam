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

export const MIC_STATUS_RUN = 0x1;
export const MIC_STATUS_PRESENT = 0x4;

/** IAudioPipe over block pipe 0xA1 of the Classic data port. Call only on the device queue. */
export class AudioPipeOverClassicDataPort implements IAudioPipe {
    private readonly _dataPort: IFPGADataPortClassic;

    constructor(dataPort: IFPGADataPortClassic) {
        this._dataPort = dataPort;
    }

    async available(): Promise<number> {
        await this._dataPort.updateWireOuts();
        return this._dataPort.getWireOutValue(MIC_WO_LEVEL) >>> 0;
    }

    async read(buffer: Uint8Array): Promise<void> {
        const got = await this._dataPort.readFromBlockPipeOut(AUDIO_PIPE_ADDRESS, AUDIO_BLOCK_SIZE, buffer);
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
