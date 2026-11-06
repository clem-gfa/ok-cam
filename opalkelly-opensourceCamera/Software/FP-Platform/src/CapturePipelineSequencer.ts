/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { WorkQueue } from "@opalkelly/frontpanel-platform-api";

import { IAxiLite, IAxiStream } from "./IAxi";
import { ICameraControl } from "./ICameraControl";
import { IISP } from "./IISP";
import { ITPG } from "./ITPG";

import { CameraMode, IMatrixDimensions } from "./CameraTypes";
import { sleep } from "./Utilities";

import { VideoDMADriver } from "./VideoDMADriver";
import { HistogramDriver } from "./HistogramDriver";
import { StreamSwitchDriver } from "./StreamSwitchDriver";

// AXI-Stream Video delivers packed 3 bytes per pixel in GBR order:
//   byte 0 = G, byte 1 = B, byte 2 = R.
// The sequencer hands this raw wire buffer through unchanged; the view
// performs the single GBR → RGBA transform into its canvas ImageData.
const BYTES_PER_PIXEL = 3;
const DDR_BASE_ADDR = 0x80000000;

// Timeout for AXI-Stream read operations (ms)
const STREAM_TIMEOUT_MS = 5000;

// Histogram sample count: 256 bins * 3 channels (RGB) = 768 u32 samples
const HISTOGRAM_SAMPLES = 256 * 3;

/**
 * A single captured frame containing both video and histogram data.
 *
 * Both are captured atomically within one pipeline read and delivered
 * together to the consumer via the handler registered with startCapture().
 */
export interface CapturedFrame {
    /** Packed 3-byte-per-pixel GBR pixel buffer, width*height*3 bytes. */
    image: Uint8Array;
    width: number;
    height: number;
    histogram: Uint32Array;     // 768 u32 samples (256 per channel, GBR order)
    frameChanged: boolean;      // content hash differs from previous frame
    timestamp: number;          // performance.now() at capture
}

/**
 * Handler invoked with each captured frame while the capture loop is running.
 */
export type CaptureHandler = (frame: CapturedFrame) => void | Promise<void>;

/**
 * Capture pipeline sequencer.
 *
 * Sequences the AMD/Xilinx capture pipeline: enforces start/stop ordering
 * of IP cores (VDMA, ISP, TPG, histogram, stream switch), manages pipeline
 * reset timing, and coordinates frame/histogram data flow to prevent
 * backpressure stalls.
 *
 * "Capture pipeline" is AMD's term for a chain of video IPs that receives
 * frames from a source and writes them to DDR memory.
 *
 * Used by both native AXI (SZG-HUB1450-AU10P) and Classic-over-bridge
 * (XEM8320) paths via the IAxiLite / IAxiStream abstractions.
 */
export class CapturePipelineSequencer {
    private readonly _axiLite: IAxiLite;
    private readonly _axiStream: IAxiStream;
    private readonly _cameraMode: CameraMode;
    private readonly _cameraControl: ICameraControl;
    private readonly _workQueue: WorkQueue;

    // IP core interfaces (shared with UI for direct configuration)
    private readonly _tpg: ITPG;
    private readonly _isp: IISP;

    // Sequencing-critical sub-drivers (internal only)
    private readonly _vdma: VideoDMADriver;
    private readonly _histogram: HistogramDriver;
    private readonly _streamSwitch: StreamSwitchDriver;

    // Frame dimensions (set by camera control via setFrameDimensions)
    private _frameDimensions: IMatrixDimensions;

    // Pipeline resolution and running state
    private _width: number;
    private _height: number;
    private _pipelineRunning: boolean;

    // Frame change detection: fast hash to detect unique frames
    private _prevFrameHash: number = 0;

    // Capture loop state
    private _captureActive: boolean = false;
    private _handler: CaptureHandler | null = null;
    private _loopPromise: Promise<void> | null = null;

    /**
     * @param axiLite        AXI-Lite interface for sub-driver register access.
     * @param axiStream      AXI-Stream interface for frame/histogram data transfer.
     * @param cameraMode     Operating mode (szgcam, pcam, tpg).
     * @param isp            ISP IP core interface (shared with UI).
     * @param tpg            TPG IP core interface (shared with UI).
     * @param cameraControl  Camera sensor control — called by the sequencer
     *                       to reinitialize I2C after system reset and to
     *                       re-apply exposure after pipeline restarts.
     * @param workQueue      Serializes hardware access across the capture loop
     *                       and other device operations (gain changes, resets).
     */
    constructor(
        axiLite: IAxiLite,
        axiStream: IAxiStream,
        cameraMode: CameraMode,
        isp: IISP,
        tpg: ITPG,
        cameraControl: ICameraControl,
        workQueue: WorkQueue
    ) {
        this._axiLite = axiLite;
        this._axiStream = axiStream;
        this._cameraMode = cameraMode;
        this._cameraControl = cameraControl;
        this._workQueue = workQueue;

        // IP core interfaces (shared with UI for direct configuration)
        this._isp = isp;
        this._tpg = tpg;

        // Sequencing-critical sub-drivers (internal only)
        this._vdma = new VideoDMADriver(axiLite);
        this._histogram = new HistogramDriver(axiLite);
        this._streamSwitch = new StreamSwitchDriver(axiLite);

        this._frameDimensions = { columnCount: 0, rowCount: 0 };

        // Resolution is set by camera control via setResolution() before pipeline start
        this._width = 0;
        this._height = 0;
        this._pipelineRunning = false;
    }

    // ================================================================
    // Accessors
    // ================================================================

    public get frameDimensions(): IMatrixDimensions {
        return this._frameDimensions;
    }

    // ================================================================
    // Pipeline Configuration
    // ================================================================

    public setResolution(width: number, height: number): void {
        this._width = width;
        this._height = height;
    }

    // ================================================================
    // Pipeline Lifecycle
    // ================================================================

    public async initializePipeline(): Promise<void> {
        // No initialization needed for the streaming pipeline
    }

    public async setFrameDimensions(dimensions: IMatrixDimensions): Promise<void> {
        this._frameDimensions = dimensions;
    }

    public async logicReset(): Promise<void> {
        await this.reconfigurePipeline();
    }

    /**
     * Stop the pipeline without asserting system reset.
     * I2C state remains valid. Used during sensor initialization.
     */
    public async assertPipelineResets(): Promise<void> {
        if (this._pipelineRunning) {
            if (!await this._vdma.stopWriteChannel()) {
                console.warn("assertPipelineResets: S2MM channel did not halt");
            }
            if (!await this._vdma.stopReadChannel()) {
                console.warn("assertPipelineResets: MM2S channel did not halt");
            }
            await this._tpg.stop();
            await this._isp.stop();
            await this._histogram.stop();
            await this._vdma.softReset();
            this._pipelineRunning = false;
        }
    }

    /**
     * Stop the pipeline and assert system reset.
     * WARNING: Invalidates I2C state.
     */
    public async stopPipeline(): Promise<void> {
        if (!await this._vdma.stopWriteChannel()) {
            console.warn("stopPipeline: S2MM channel did not halt, continuing with reset");
        }
        if (!await this._vdma.stopReadChannel()) {
            console.warn("stopPipeline: MM2S channel did not halt, continuing with reset");
        }

        await this._tpg.stop();
        await this._isp.stop();
        await this._histogram.stop();

        await this._axiLite.resetSystem();
        await this._vdma.softReset();

        // Allow IPs to exit reset cleanly. resetSystem() asserts axis_aresetn
        // which resets stream-domain IPs (histogram, stream switch, dwidth converter).
        // In the szgcam design, vid_clk comes from the camera sensor LVDS clock
        // (syzygy_camera_phy → BUFGCE_DIV). In the pcam design (used for TPG mode),
        // vid_clk comes from clk_wiz_0 and is always available.
        await sleep(100);

        this._pipelineRunning = false;
    }

    // ================================================================
    // Frame Capture
    // ================================================================

    /**
     * Start the capture loop. Pulls frames continuously and invokes the
     * handler with each CapturedFrame. Throws if a loop is already running.
     */
    public startCapture(handler: CaptureHandler): void {
        if (this._loopPromise !== null) {
            throw new Error("CapturePipelineSequencer: capture loop already running");
        }
        this._captureActive = true;
        this._handler = handler;
        this._loopPromise = this._runCaptureLoop();
    }

    /**
     * Stop the capture loop. Awaits the current iteration before returning,
     * so no capture work is in flight when the caller proceeds.
     */
    public async stopCapture(): Promise<void> {
        this._captureActive = false;
        if (this._loopPromise) {
            await this._loopPromise;
            this._loopPromise = null;
        }
        this._handler = null;
    }

    /**
     * Capture a single frame on demand. Discards the first (stale triple-buffer)
     * frame and returns the second. Both reads happen inside one queue post so
     * the loop cannot interleave between the discard and the fresh read.
     */
    public async captureOnce(): Promise<CapturedFrame | null> {
        let result: CapturedFrame | null = null;
        await this._workQueue.post(async () => {
            await this._captureFrameInternal();                 // discard stale
            result = await this._captureFrameInternal();        // return fresh
        });
        return result;
    }

    private async _runCaptureLoop(): Promise<void> {
        while (this._captureActive) {
            let frame: CapturedFrame | null = null;

            // Step 1: Pull one frame from hardware.
            try {
                await this._workQueue.post(async () => {
                    frame = await this._captureFrameInternal();
                });
            } catch (err) {
                console.error("CapturePipelineSequencer: capture iteration failed", err);
            }

            // Step 2: Hand the frame to the consumer.
            if (frame && this._captureActive && this._handler) {
                try {
                    await this._handler(frame);
                } catch (err) {
                    console.error("CapturePipelineSequencer: handler failed", err);
                }
            }

            if (!this._captureActive) break;

            // Step 3: Yield to the event loop briefly, then repeat.
            await sleep(2);
        }
    }

    private async _captureFrameInternal(): Promise<CapturedFrame | null> {
        if (!this._pipelineRunning) {
            console.warn("CapturePipelineSequencer: captureFrame() skipped — pipeline not running");
            return null;
        }

        const width = this._width;
        const height = this._height;
        const numPixels = width * height;

        // Route video (SI0) and pull the frame straight off the wire.
        // Fresh buffer per frame so the handler can hold onto this image
        // past the next capture iteration.
        await this._streamSwitch.setSlave(0);
        const image = new Uint8Array(numPixels * BYTES_PER_PIXEL);
        await this._axiStream.read(image, STREAM_TIMEOUT_MS);

        // Fast hash to detect whether the frame content actually changed.
        // Sample every 1024th byte and accumulate a simple checksum.
        let hash = 0;
        for (let i = 0; i < image.length; i += 1024) {
            hash = (hash * 31 + image[i]) | 0;
        }
        const frameChanged = hash !== this._prevFrameHash;
        this._prevFrameHash = hash;

        // The gateware block has one image input and two AXI-Stream outputs:
        // the untouched image and a computed histogram. It will not emit the
        // histogram until the image is drained, and will not emit the next
        // image until the histogram is drained.
        //
        // Fresh Uint32Array per frame so consecutive iterations don't
        // overwrite samples the previous handler is still consuming.
        // HISTOGRAM_SAMPLES = 768 (3 channels × 256 bins).
        const histogram = new Uint32Array(HISTOGRAM_SAMPLES);
        await this._streamSwitch.setSlave(1);
        await this._axiStream.read(histogram, STREAM_TIMEOUT_MS);

        return {
            image,
            width,
            height,
            histogram,
            frameChanged,
            timestamp: performance.now(),
        };
    }

    // ================================================================
    // Private: Pipeline Sequencing
    // ================================================================

    /**
     * Reconfigure the full video pipeline for the current resolution and ISP settings.
     * Ported from Python camera_app.py change_resolution().
     *
     * NOTE: This calls resetSystem() which asserts axis_aresetn. In the szgcam
     * design this resets the camera PHY via util_vector_logic_2 (vid_clk comes from
     * sensor LVDS clock — requires sensor streaming to recover). In the pcam design
     * (used for TPG mode) vid_clk comes from clk_wiz and is always available.
     */
    private async reconfigurePipeline(): Promise<void> {
        const width = this._width;
        const height = this._height;
        const horizontalSizeBytes = width * BYTES_PER_PIXEL;
        const frameSize = horizontalSizeBytes * height;

        const buf0 = DDR_BASE_ADDR;
        const buf1 = buf0 + frameSize;
        const buf2 = buf1 + frameSize;

        console.log(
            `CapturePipelineSequencer: Reconfiguring pipeline ${width}x${height}, frameSize=0x${frameSize.toString(16)}`
        );

        try {
            await this.stopPipeline();
            await this._cameraControl.reinitializeI2C();
            await this.configureIPs(width, height);
            await this.startupPipeline(horizontalSizeBytes, height, buf0, buf1, buf2, width);

            this._pipelineRunning = true;
            console.log("CapturePipelineSequencer: Pipeline running.");

            await this._cameraControl.setExposure(this._cameraControl.exposure);
        } catch (error) {
            this._pipelineRunning = false;
            throw error;
        }
    }

    /** Configure TPG, ISP, and Histogram IPs for the given resolution. */
    private async configureIPs(width: number, height: number): Promise<void> {
        await this._tpg.setResolution(width, height);
        await this._tpg.setPattern(this._tpg.patternId);
        await this._tpg.setMotionSpeed(this._tpg.motionSpeed);

        await this._isp.initialize(width, height, this._isp.awb, this._isp.rgain, this._isp.ggain, this._isp.bgain);

        await this._histogram.initialize(height, width);

        // NOTE: Stream switch is intentionally NOT configured here. After reset,
        // ROUTING_MODE=1 disables all routes (MI0_MUX=0x80000000). This keeps the
        // pipeline stalled until flushFrame() explicitly configures the switch and
        // reads data, matching the Python reference behavior. Pre-configuring the
        // switch would allow premature data flow during VDMAIntErr wait, potentially
        // corrupting the VDMA genlock state.
    }

    /**
     * Start VDMA and IPs, wait for expected VDMAIntErr, flush first frame.
     * Order matters: S2MM first, then IPs, then MM2S.
     */
    private async startupPipeline(
        horizontalSizeBytes: number,
        height: number,
        buf0: number,
        buf1: number,
        buf2: number,
        width: number
    ): Promise<void> {
        await this._vdma.startWriteChannel(horizontalSizeBytes, height, buf0, buf1, buf2);
        await this._tpg.start(this._cameraMode !== "tpg");
        await this._isp.start();
        await this._histogram.start();
        await this._vdma.startReadChannel(horizontalSizeBytes, height, buf0, buf1, buf2);

        // Diagnostic: Check VDMA status after pipeline start
        const s2mmSr = await this._vdma.getWriteChannelStatus();
        const mm2sSr = await this._vdma.getReadChannelStatus();
        console.log(`CapturePipelineSequencer: Post-start VDMA status S2MM=0x${s2mmSr.toString(16)} MM2S=0x${mm2sSr.toString(16)}`);

        // Wait for expected VDMAIntErr and clear status
        for (let i = 0; i < 10; i++) {
            await sleep(50);
            const sr = await this._vdma.getWriteChannelStatus();

            if (sr & (1 << 4)) {
                console.log("CapturePipelineSequencer: VDMAIntErr detected (expected)");
                break;
            }
        }

        await this._vdma.clearStatus();

        // Flush first frame (buffer 0 is never written in triple-buffer mode)
        console.log("CapturePipelineSequencer: Starting flushFrame...");
        await this.flushFrame(width, height);
    }

    private async flushFrame(width: number, height: number): Promise<void> {
        const frameSize = width * height * BYTES_PER_PIXEL;
        const frameBuffer = new Uint8Array(frameSize);

        // Flush video frame
        await this._streamSwitch.setSlave(0);
        console.log(`CapturePipelineSequencer: flushFrame reading video (${frameSize} bytes)...`);
        await this._axiStream.read(frameBuffer, STREAM_TIMEOUT_MS);
        console.log("CapturePipelineSequencer: flushFrame video read complete.");

        // Flush histogram
        const histogramBuffer = new Uint32Array(HISTOGRAM_SAMPLES);
        await this._streamSwitch.setSlave(1);
        console.log(`CapturePipelineSequencer: flushFrame reading histogram (${HISTOGRAM_SAMPLES} samples)...`);
        await this._axiStream.read(histogramBuffer, STREAM_TIMEOUT_MS);
        console.log("CapturePipelineSequencer: flushFrame histogram read complete.");
    }
}
