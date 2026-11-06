/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/**
 * Interface for the Video Test Pattern Generator IP core.
 *
 * Configuration methods (setPattern, setMotionSpeed) are safe to call at any
 * time — they perform single AXI-Lite register writes with no sequencing
 * dependencies. The UI calls these directly.
 *
 * Lifecycle methods (setResolution, start, stop) are called by the capture
 * sequencer during pipeline sequencing.
 */
export interface ITPG {
    readonly patternId: number;
    readonly motionSpeed: number;

    // Configuration — safe to call from UI at any time
    setPattern(patternId: number): Promise<void>;
    setMotionSpeed(speed: number): Promise<void>;

    // Lifecycle — called by sequencer during pipeline sequencing
    setResolution(width: number, height: number): Promise<void>;
    start(enableInput: boolean): Promise<void>;
    stop(): Promise<void>;
}
