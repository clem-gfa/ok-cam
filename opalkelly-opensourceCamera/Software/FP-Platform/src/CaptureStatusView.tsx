/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { Component, ReactNode } from "react";

import "./CaptureStatusView.css";

import { CapturedFrame } from "./CapturePipelineSequencer";

interface CaptureStatusViewState {
    systemFps: number;
    cameraFps: number;
}

class CaptureStatusView extends Component<{}, CaptureStatusViewState> {
    private readonly _frameTimeStamps: number[] = [];
    private readonly _cameraTimeStamps: number[] = [];

    constructor(props: {}) {
        super(props);
        this.state = { systemFps: 0, cameraFps: 0 };
    }

    /**
     * Update FPS counters with a newly captured frame.
     *
     * System FPS = rate at which the host is pulling frames from the FPGA.
     * Camera FPS = rate at which the sensor is producing unique frames
     * (detected via content hash in CapturedFrame.frameChanged).
     */
    public updateStats(frame: CapturedFrame): void {
        // --- System FPS (host-side throughput) ---
        this._frameTimeStamps.push(frame.timestamp);

        if (this._frameTimeStamps.length > 20) {
            this._frameTimeStamps.shift();
        }

        let systemFps = this.state.systemFps;
        if (this._frameTimeStamps.length > 1) {
            const totalTimeMilliseconds =
                this._frameTimeStamps[this._frameTimeStamps.length - 1] - this._frameTimeStamps[0];
            const avgFrameTime = totalTimeMilliseconds / (this._frameTimeStamps.length - 1);
            systemFps = Math.round(1000 / avgFrameTime);
        }

        // --- Camera FPS (unique frames only, detected via content hash) ---
        let cameraFps = this.state.cameraFps;
        if (frame.frameChanged) {
            this._cameraTimeStamps.push(frame.timestamp);

            if (this._cameraTimeStamps.length > 20) {
                this._cameraTimeStamps.shift();
            }

            if (this._cameraTimeStamps.length > 1) {
                const totalTimeMilliseconds =
                    this._cameraTimeStamps[this._cameraTimeStamps.length - 1] - this._cameraTimeStamps[0];
                const avgFrameTime = totalTimeMilliseconds / (this._cameraTimeStamps.length - 1);
                cameraFps = Math.round(1000 / avgFrameTime);
            }
        }

        this.setState({ systemFps, cameraFps });
    }

    render(): ReactNode {
        return (
            <div className="okBufferStatusPanel">
                <div className="okRowPanel">
                    <div className="okCaptureStatusField">
                        <span className="okLabelText okLabelMuted">
                            Camera FPS
                        </span>
                        <div className="okValueBox">
                            {this.state.cameraFps}
                        </div>
                    </div>
                </div>
                <div className="okRowPanel">
                    <div className="okCaptureStatusField">
                        <span className="okLabelText okLabelMuted">
                            System FPS
                        </span>
                        <div className="okValueBox">
                            {this.state.systemFps}
                        </div>
                    </div>
                </div>
            </div>
        );
    }
}

export default CaptureStatusView;
