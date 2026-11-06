/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/**
 * Interface for the Image Signal Processor IP core.
 *
 * Configuration methods (setGains, setAWBThreshold) are safe to call at any
 * time — they perform single AXI-Lite register writes with no sequencing
 * dependencies. The UI calls these directly.
 *
 * Lifecycle methods (initialize, start, stop) are called by the capture
 * sequencer during pipeline sequencing.
 */
export interface IISP {
    readonly rgain: number;
    readonly ggain: number;
    readonly bgain: number;
    readonly awb: number;

    // Configuration — safe to call from UI at any time
    setGains(rgain: number, ggain: number, bgain: number): Promise<void>;
    setAWBThreshold(awb: number): Promise<void>;

    // Lifecycle — called by sequencer during pipeline sequencing
    initialize(
        width: number,
        height: number,
        awbThresh: number,
        rgain: number,
        ggain: number,
        bgain: number
    ): Promise<void>;
    start(): Promise<void>;
    stop(): Promise<void>;
}
