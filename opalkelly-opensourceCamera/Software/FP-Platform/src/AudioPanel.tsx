/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Microphone panel (XEM8310 / DARWIN, 64 PDM microphones): start/stop the microphones, 64 sliding waveforms
// (8 x 8 grid, min/max envelope per pixel column) and recording to numbered segments exported to the user.

import React from "react";

import { IFPGADataPortClassic, WorkQueue } from "@opalkelly/frontpanel-platform-api";

import { AxiStreamOverClassicDataPort } from "./AxiOverClassicDataPort";
import { AUDIO_CHANNELS, AUDIO_PEAK_BLOCK, AudioRecorder, AudioStats } from "./AudioStream";
import { AudioPipeOverClassicDataPort, MIC_STATUS_RUN, MicCounters, micCounters, micPresent, micSetEnabled, micStatus } from "./AudioPipe";
import { OpfsExporter, opfsSinkFactory, purgeOpfs, storageInfo } from "./OpfsStorage";

const FULL_SCALE = 8388608; // 2^23
// A recording is cut into files of this duration (~36 MB/s: 20 s = ~730 MB). The OPFS quota is a few GB and a finished
// segment stays there until it has been handed to the user, so small segments keep the footprint low.
const SEGMENT_SECONDS = 20;
const AUDIO_BYTES_PER_SECOND = 36.3e6;

interface AudioPanelProps {
    dataPort: IFPGADataPortClassic;
    stream: AxiStreamOverClassicDataPort;
    workQueue: WorkQueue;
    /** Shown (the panel stays mounted while hidden, so the acquisition keeps running). */
    visible: boolean;
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

const AudioPanel: React.FC<AudioPanelProps> = ({ dataPort, stream, workQueue, visible }) => {
    const [present, setPresent] = React.useState<boolean | null>(null);
    const [running, setRunning] = React.useState(false);
    const [recording, setRecording] = React.useState(false);
    const [windowMs, setWindowMs] = React.useState(20);
    const [gain, setGain] = React.useState(8);
    const [stats, setStats] = React.useState<AudioStats | null>(null);
    const [fpga, setFpga] = React.useState<MicCounters | null>(null);
    const [message, setMessage] = React.useState("");
    const [layout, setLayout] = React.useState<"8x8" | "16x4">("8x8");
    const [large, setLarge] = React.useState(false);
    const [size, setSize] = React.useState({ w: 640, h: 448 }); // drawing area (CSS pixels)
    const canvasRef = React.useRef<HTMLCanvasElement>(null);
    const areaRef = React.useRef<HTMLDivElement>(null);
    const recorderRef = React.useRef<AudioRecorder | null>(null);
    const settingsRef = React.useRef({ windowMs: 20, gain: 8, layout: "8x8", w: 640, h: 448 });
    settingsRef.current = { windowMs, gain, layout, w: size.w, h: size.h };

    const getRecorder = React.useCallback((): AudioRecorder => {
        if (!recorderRef.current) {
            const exporter = new OpfsExporter();
            recorderRef.current = new AudioRecorder(new AudioPipeOverClassicDataPort(dataPort), {
                keepRaw: false, // the display uses the per-block peaks only
                segmentSeconds: SEGMENT_SECONDS,
                catchUp: true, // behind and not recording: show the newest audio instead of a growing delay
                runOnDevice: (task) => workQueue.post(task),
                onSegmentClosed: async (wav, csv) => {
                    await exporter.exportFile(wav);
                    await exporter.exportFile(csv);
                },
            });
        }
        return recorderRef.current;
    }, [dataPort, workQueue]);

    // Is the microphone subsystem in the loaded bitfile?
    React.useEffect(() => {
        if (!visible || present !== null) return;
        void workQueue
            .post(async () => {
                setPresent(await micPresent(dataPort));
            })
            .catch((e) => setMessage(String(e)));
    }, [visible, present, dataPort, workQueue]);

    const start = async () => {
        const rec = getRecorder();
        try {
            setMessage("Starting microphones (clock sequence ~35 ms)...");
            await workQueue.post(async () => micSetEnabled(dataPort, true));
            let up = false;
            for (let i = 0; i < 30 && !up; i++) {
                await sleep(100);
                await workQueue.post(async () => {
                    up = ((await micStatus(dataPort)) & MIC_STATUS_RUN) !== 0;
                });
            }
            if (!up) throw new Error("microphones did not reach the running state (check VIO3 = 1.8 V)");
            // Between slices of every large (video) transfer, service the audio FIFO.
            stream.setBetweenChunksHook(async () => {
                try {
                    await rec.pump();
                } catch (e) {
                    setMessage(`audio: ${e instanceof Error ? e.message : String(e)}`);
                }
            });
            rec.start();
            setRunning(true);
            setMessage("");
        } catch (e) {
            setMessage(e instanceof Error ? e.message : String(e));
        }
    };

    const stop = async () => {
        const rec = getRecorder();
        stream.setBetweenChunksHook(null);
        await rec.stop();
        await rec.stopRecording(); // also releases the files of a recording that failed
        setRecording(false);
        await workQueue.post(async () => micSetEnabled(dataPort, false)).catch(() => undefined);
        setRunning(false);
        setMessage(rec.error ? `stopped: ${rec.error}` : "");
    };

    const toggleRecording = async () => {
        const rec = getRecorder();
        if (rec.recording) {
            await rec.stopRecording();
            setRecording(false);
            return;
        }
        try {
            // Files left by earlier runs count against the storage quota: clear them, then check there is room for a
            // segment being written plus the previous one still waiting to be handed over.
            await purgeOpfs();
            const info = await storageInfo();
            const need = 3 * SEGMENT_SECONDS * AUDIO_BYTES_PER_SECOND;
            if (info && info.quotaBytes - info.usageBytes < need) {
                setMessage(
                    `Not enough storage for a recording: ${((info.quotaBytes - info.usageBytes) / 1e9).toFixed(1)} GB free, ` +
                        `${(need / 1e9).toFixed(1)} GB needed (quota ${(info.quotaBytes / 1e9).toFixed(1)} GB)`
                );
                return;
            }
            const d = new Date();
            const pad = (n: number) => String(n).padStart(2, "0");
            const session = `okcam_audio_${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}_${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
            rec.startRecording(session, opfsSinkFactory);
            setRecording(true);
            setMessage("");
        } catch (e) {
            setMessage(e instanceof Error ? e.message : String(e));
        }
    };

    // The drawing area follows the panel size (drag the bottom-right corner, or use Maximize)
    React.useEffect(() => {
        const el = areaRef.current;
        if (!el || !visible || typeof ResizeObserver === "undefined") return;
        const ro = new ResizeObserver((entries) => {
            const r = entries[0].contentRect;
            if (r.width > 100 && r.height > 100) setSize({ w: Math.floor(r.width), h: Math.floor(r.height) });
        });
        ro.observe(el);
        return () => ro.disconnect();
    }, [visible, present, large]);

    // Release the hardware when the panel goes away
    React.useEffect(() => {
        return () => {
            stream.setBetweenChunksHook(null);
            void recorderRef.current?.stop();
        };
    }, [stream]);

    // Drawing + statistics while running. Drawing is deliberately cheap (it reads the per-block min/max kept by
    // AudioRecorder, ~1 ms per image): the gateware FIFO only holds ~3.6 ms of audio, so a long main-thread task
    // here would make the microphone stream lose packets.
    React.useEffect(() => {
        if (!running || !visible) return;
        let raf = 0;
        let last = 0;
        let lastFpga = 0;
        const bufMin = new Int32Array(8192);
        const bufMax = new Int32Array(8192);
        const draw = (now: number) => {
            raf = requestAnimationFrame(draw);
            if (now - last < 50) return; // 20 images/s
            last = now;
            const canvas = canvasRef.current;
            const rec = recorderRef.current;
            if (!canvas || !rec) return;
            const ctx = canvas.getContext("2d");
            if (!ctx) return;
            const { windowMs: wms, gain: g, layout: lay, w: cw, h: chh } = settingsRef.current;
            const cols = lay === "16x4" ? 16 : 8;
            const rows = AUDIO_CHANNELS / cols;
            const dpr = Math.min(window.devicePixelRatio || 1, 2);
            if (canvas.width !== Math.round(cw * dpr) || canvas.height !== Math.round(chh * dpr)) {
                canvas.width = Math.round(cw * dpr);
                canvas.height = Math.round(chh * dpr);
            }
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            const CELL_W = cw / cols;
            const CELL_H = chh / rows;
            const blocks = Math.min(bufMin.length, Math.max(1, Math.round(((wms / 1000) * rec.sampleRate) / AUDIO_PEAK_BLOCK)));
            ctx.fillStyle = "#101418";
            ctx.fillRect(0, 0, cw, chh);
            const k = ((CELL_H / 2 - 2) * g) / FULL_SCALE;
            const pxCols = Math.max(8, Math.floor(CELL_W) - 2);
            for (let ch = 0; ch < AUDIO_CHANNELS; ch++) {
                const x0 = (ch % cols) * CELL_W;
                const y0 = Math.floor(ch / cols) * CELL_H;
                const mid = y0 + CELL_H / 2;
                ctx.strokeStyle = "#2a323a";
                ctx.strokeRect(x0 + 0.5, y0 + 0.5, CELL_W - 1, CELL_H - 1);
                const n = rec.recentPeaks(ch, blocks, bufMin, bufMax);
                if (n === 0) continue;
                ctx.strokeStyle = "#5ad1a0";
                ctx.beginPath();
                for (let px = 0; px < pxCols; px++) {
                    const i0 = Math.floor((px * n) / pxCols);
                    const i1 = Math.max(i0 + 1, Math.floor(((px + 1) * n) / pxCols));
                    let mn = bufMin[i0];
                    let mx = bufMax[i0];
                    for (let i = i0 + 1; i < i1 && i < n; i++) {
                        if (bufMin[i] < mn) mn = bufMin[i];
                        if (bufMax[i] > mx) mx = bufMax[i];
                    }
                    const yTop = Math.max(y0 + 1, mid - mx * k);
                    const yBot = Math.min(y0 + CELL_H - 1, mid - mn * k);
                    ctx.moveTo(x0 + 1 + px + 0.5, yTop);
                    ctx.lineTo(x0 + 1 + px + 0.5, Math.max(yBot, yTop + 1));
                }
                ctx.stroke();
                ctx.fillStyle = "#7d8790";
                ctx.font = "9px monospace";
                ctx.fillText(String(ch + 1), x0 + 3, y0 + 10);
            }
            setStats(rec.stats);
            if (rec.recordError) {
                setMessage(`Recording stopped: ${rec.recordError}`);
                setRecording(false);
            }
            if (now - lastFpga > 1000) {
                lastFpga = now;
                void workQueue.post(async () => setFpga(await micCounters(dataPort))).catch(() => undefined);
            }
        };
        raf = requestAnimationFrame(draw);
        return () => cancelAnimationFrame(raf);
    }, [running, visible, dataPort, workQueue]);

    const buttonStyle: React.CSSProperties = { padding: "4px 8px", marginRight: 6 };
    // The panel lives in a draggable frame (CameraView). Drag the bottom-right corner to resize, or use Large.
    const width = large ? Math.max(700, Math.min(window.innerWidth - 160, 1500)) : 640;
    const height = large ? Math.max(480, Math.min(window.innerHeight - 180, 900)) : 520;
    return (
                <div
                    style={{
                        color: "#ddd",
                        font: "12px monospace",
                        boxSizing: "border-box",
                        display: "flex",
                        flexDirection: "column",
                        overflow: "hidden",
                        resize: "both",
                        width,
                        height,
                        minWidth: 420,
                        minHeight: 300,
                    }}
                >
                    <div style={{ marginBottom: 6 }}>
                        <button style={{ float: "right" }} onClick={() => setLarge(!large)}>
                            {large ? "Normal size" : "Large"}
                        </button>
                    </div>
                    {present === false && (
                        <div>The loaded bitfile has no microphone subsystem (build the gateware with <code>set with_mics 1</code>).</div>
                    )}
                    {present === null && <div>Checking the bitfile...</div>}
                    {present && (
                        <>
                            <div style={{ marginBottom: 6 }}>
                                {!running ? (
                                    <button style={buttonStyle} onClick={start}>
                                        Start microphones
                                    </button>
                                ) : (
                                    <button style={buttonStyle} onClick={stop}>
                                        Stop
                                    </button>
                                )}
                                <button style={buttonStyle} onClick={toggleRecording} disabled={!running}>
                                    {recording ? "Stop recording" : "Record"}
                                </button>
                                Window{" "}
                                <select value={windowMs} onChange={(e) => setWindowMs(Number(e.target.value))}>
                                    <option value={5}>5 ms</option>
                                    <option value={20}>20 ms</option>
                                    <option value={100}>100 ms</option>
                                </select>{" "}
                                Gain{" "}
                                <select value={gain} onChange={(e) => setGain(Number(e.target.value))}>
                                    <option value={1}>x1</option>
                                    <option value={8}>x8</option>
                                    <option value={64}>x64</option>
                                    <option value={512}>x512</option>
                                </select>{" "}
                                Layout{" "}
                                <select value={layout} onChange={(e) => setLayout(e.target.value as "8x8" | "16x4")}>
                                    <option value="8x8">8 x 8</option>
                                    <option value="16x4">16 x 4</option>
                                </select>
                            </div>
                            <div ref={areaRef} style={{ position: "relative", flex: 1, minHeight: 0 }}>
                                <canvas ref={canvasRef} style={{ position: "absolute", left: 0, top: 0, width: "100%", height: "100%" }} />
                            </div>
                            {stats && (
                                <div style={{ marginTop: 4 }}>
                                    packets {stats.packets} | lost {stats.lostPackets} (
                                    {((100 * stats.lostPackets) / Math.max(1, stats.packets + stats.lostPackets)).toFixed(1)}%) | bad {stats.badBlocks} |
                                    host gap max {stats.maxGapMs.toFixed(1)} ms, {stats.longGaps} over 3 ms
                                    {stats.elapsedMs > 1000
                                        ? ` | busy: pipe ${((100 * stats.readMs) / stats.elapsedMs).toFixed(0)}%, decode ${((100 * stats.decodeMs) / stats.elapsedMs).toFixed(0)}%, rate ${((stats.packets * 4096) / 1e6 / (stats.elapsedMs / 1000)).toFixed(1)} MB/s (need 36)`
                                        : ""}
                                    {stats.skippedPackets > 0 ? ` | skipped ${stats.skippedPackets} (catching up)` : ""}
                                    {recording || stats.segments > 0
                                        ? ` | rec ${(stats.recordedFrames / (recorderRef.current?.sampleRate ?? 1)).toFixed(1)} s, ${stats.segments} segment(s) exported`
                                        : ""}
                                </div>
                            )}
                            {fpga && (
                                <div style={{ marginTop: 2, color: "#9aa5b1" }}>
                                    FPGA: dropped {fpga.dropped} | overruns {fpga.overruns} | FIFO {((fpga.level * 4) / 1024).toFixed(0)} KB |
                                    {(fpga.status & 0x8) !== 0 ? " FIFO was full" : " FIFO never full"}
                                    {fpga.ringPeak > 0 && recorderRef.current
                                        ? ` | DDR4 ring: ${fpga.ringUsed} pk now, peak ${fpga.ringPeak} pk = ${((fpga.ringPeak * 21 * 1000) / recorderRef.current.sampleRate).toFixed(0)} ms of backlog`
                                        : ""}
                                </div>
                            )}
                        </>
                    )}
                    {message && <div style={{ color: "#ffb86b", marginTop: 4 }}>{message}</div>}
                </div>
    );
};

export default AudioPanel;
