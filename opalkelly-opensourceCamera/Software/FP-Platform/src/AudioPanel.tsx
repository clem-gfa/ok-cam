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
import { OpfsExporter, opfsSinkFactory } from "./OpfsStorage";

const COLS = 8;
const CELL_W = 80;
const CELL_H = 56;
const FULL_SCALE = 8388608; // 2^23

interface AudioPanelProps {
    dataPort: IFPGADataPortClassic;
    stream: AxiStreamOverClassicDataPort;
    workQueue: WorkQueue;
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

const AudioPanel: React.FC<AudioPanelProps> = ({ dataPort, stream, workQueue }) => {
    const [open, setOpen] = React.useState(false);
    const [present, setPresent] = React.useState<boolean | null>(null);
    const [running, setRunning] = React.useState(false);
    const [recording, setRecording] = React.useState(false);
    const [windowMs, setWindowMs] = React.useState(20);
    const [gain, setGain] = React.useState(8);
    const [stats, setStats] = React.useState<AudioStats | null>(null);
    const [fpga, setFpga] = React.useState<MicCounters | null>(null);
    const [message, setMessage] = React.useState("");
    const canvasRef = React.useRef<HTMLCanvasElement>(null);
    const recorderRef = React.useRef<AudioRecorder | null>(null);
    const settingsRef = React.useRef({ windowMs: 20, gain: 8 });
    settingsRef.current = { windowMs, gain };

    const getRecorder = React.useCallback((): AudioRecorder => {
        if (!recorderRef.current) {
            const exporter = new OpfsExporter();
            recorderRef.current = new AudioRecorder(new AudioPipeOverClassicDataPort(dataPort), {
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
        if (!open || present !== null) return;
        void workQueue
            .post(async () => {
                setPresent(await micPresent(dataPort));
            })
            .catch((e) => setMessage(String(e)));
    }, [open, present, dataPort, workQueue]);

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
        if (rec.recording) {
            await rec.stopRecording();
            setRecording(false);
        }
        await workQueue.post(async () => micSetEnabled(dataPort, false)).catch(() => undefined);
        setRunning(false);
        setMessage(rec.error ? `stopped: ${rec.error}` : "");
    };

    const toggleRecording = async () => {
        const rec = getRecorder();
        if (rec.recording) {
            await rec.stopRecording();
            setRecording(false);
        } else {
            const d = new Date();
            const pad = (n: number) => String(n).padStart(2, "0");
            const session = `okcam_audio_${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}_${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
            rec.startRecording(session, opfsSinkFactory);
            setRecording(true);
        }
    };

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
        if (!running || !open) return;
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
            const { windowMs: wms, gain: g } = settingsRef.current;
            const blocks = Math.min(bufMin.length, Math.max(1, Math.round(((wms / 1000) * rec.sampleRate) / AUDIO_PEAK_BLOCK)));
            ctx.fillStyle = "#101418";
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            const k = ((CELL_H / 2 - 2) * g) / FULL_SCALE;
            const cols = CELL_W - 2;
            for (let ch = 0; ch < AUDIO_CHANNELS; ch++) {
                const x0 = (ch % COLS) * CELL_W;
                const y0 = Math.floor(ch / COLS) * CELL_H;
                const mid = y0 + CELL_H / 2;
                ctx.strokeStyle = "#2a323a";
                ctx.strokeRect(x0 + 0.5, y0 + 0.5, CELL_W - 1, CELL_H - 1);
                const n = rec.recentPeaks(ch, blocks, bufMin, bufMax);
                if (n === 0) continue;
                ctx.strokeStyle = "#5ad1a0";
                ctx.beginPath();
                for (let px = 0; px < cols; px++) {
                    const i0 = Math.floor((px * n) / cols);
                    const i1 = Math.max(i0 + 1, Math.floor(((px + 1) * n) / cols));
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
            if (now - lastFpga > 1000) {
                lastFpga = now;
                void workQueue.post(async () => setFpga(await micCounters(dataPort))).catch(() => undefined);
            }
        };
        raf = requestAnimationFrame(draw);
        return () => cancelAnimationFrame(raf);
    }, [running, open, dataPort, workQueue]);

    const buttonStyle: React.CSSProperties = { padding: "4px 8px", marginRight: 6 };
    return (
        <>
            <button
                onClick={() => setOpen(!open)}
                style={{ position: "fixed", left: 130, bottom: 8, zIndex: 100, padding: "4px 8px" }}
            >
                Audio
            </button>
            {open && (
                <div
                    style={{
                        position: "fixed",
                        top: 48,
                        right: 8,
                        zIndex: 100,
                        background: "rgba(16,20,24,0.94)",
                        color: "#ddd",
                        font: "12px monospace",
                        padding: 8,
                        width: COLS * CELL_W + 16,
                    }}
                >
                    <div style={{ marginBottom: 6 }}>
                        <b>Microphones (64 channels)</b>
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
                                </select>
                            </div>
                            <canvas ref={canvasRef} width={COLS * CELL_W} height={(AUDIO_CHANNELS / COLS) * CELL_H} />
                            {stats && (
                                <div style={{ marginTop: 4 }}>
                                    packets {stats.packets} | lost {stats.lostPackets} | bad {stats.badBlocks}
                                    {recording || stats.segments > 0
                                        ? ` | rec ${(stats.recordedFrames / (recorderRef.current?.sampleRate ?? 1)).toFixed(1)} s, ${stats.segments} segment(s) exported`
                                        : ""}
                                </div>
                            )}
                            {fpga && (
                                <div style={{ marginTop: 2, color: "#9aa5b1" }}>
                                    FPGA: dropped {fpga.dropped} | overruns {fpga.overruns} | FIFO {Math.round((fpga.level / 32768) * 100)}% |
                                    {(fpga.status & 0x8) !== 0 ? " FIFO was full" : " FIFO never full"}
                                </div>
                            )}
                        </>
                    )}
                    {message && <div style={{ color: "#ffb86b", marginTop: 4 }}>{message}</div>}
                </div>
            )}
        </>
    );
};

export default AudioPanel;
