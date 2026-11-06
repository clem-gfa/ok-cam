/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { DataBuffer } from "@opalkelly/frontpanel-platform-api";

/**
 * AXI-Lite register access interface.
 *
 * Abstracts the underlying communication mechanism (native AXI-Lite
 * data port, Classic FrontPanel bridge, etc.) from the IP sub-drivers
 * and I2C controller that need to read/write FPGA registers.
 */
export interface IAxiLite {
    /** Read a 32-bit register at the given byte address. */
    read32(address: number): Promise<number>;

    /** Write a 32-bit value to the register at the given byte address. */
    write32(address: number, value: number): Promise<void>;

    /**
     * Assert the AXI system reset line (axis_aresetn).
     *
     * In the native AXI path this delegates to axiStream.reset().
     * In the Classic path this toggles WireIn 0x00.
     *
     * WARNING: This invalidates I2C state — callers must re-initialize
     * the I2C controller after calling this method.
     */
    resetSystem(): Promise<void>;
}

/**
 * AXI-Stream data access interface (read-only).
 *
 * Abstracts AXI-Stream reads (native data port) or BlockPipeOut reads
 * (Classic data port) behind a single interface consumed by the pipeline.
 */
export interface IAxiStream {
    /**
     * Read streaming data into `buffer`. The transfer length is determined
     * by `buffer.byteLength`. Throws on timeout.
     */
    read(buffer: DataBuffer, timeoutMs: number): Promise<void>;
}
