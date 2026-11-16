/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Temporary diagnostic: which file-writing mechanisms does the FrontPanel app host allow?
// The FrontPanel Platform API has no file API, so recording audio/video to disk from the TypeScript app
// depends on what the embedded web view lets a page do. The results are shown on screen (no console needed).

import React from "react";

type SaveFilePickerWindow = Window & {
    showSaveFilePicker?: (options?: { suggestedName?: string }) => Promise<{
        createWritable: () => Promise<{ write: (data: Uint8Array<ArrayBuffer>) => Promise<void>; close: () => Promise<void> }>;
    }>;
};

const CHUNK_BYTES = 4 * 1024 * 1024;
const TOTAL_BYTES = 128 * 1024 * 1024; // the 64-channel audio stream alone is ~36 MB/s

function makeChunk(): Uint8Array<ArrayBuffer> {
    const chunk = new Uint8Array(new ArrayBuffer(CHUNK_BYTES));
    for (let i = 0; i < chunk.length; i += 4096) chunk[i] = i >> 12;
    return chunk;
}

function mbps(bytes: number, ms: number): string {
    return `${(bytes / 1e6 / (ms / 1000)).toFixed(0)} MB/s`;
}

async function testDownload(): Promise<string> {
    const blob = new Blob([makeChunk()], { type: "application/octet-stream" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "okcam_probe_download.bin";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
    return "triggered (4 MB, <a download>): check whether a file okcam_probe_download.bin appeared in the Downloads folder";
}

async function testPicker(): Promise<string> {
    const w = window as SaveFilePickerWindow;
    if (typeof w.showSaveFilePicker !== "function") return "NOT AVAILABLE (showSaveFilePicker is missing)";
    const handle = await w.showSaveFilePicker({ suggestedName: "okcam_probe_picker.bin" });
    const writable = await handle.createWritable();
    const chunk = makeChunk();
    const start = performance.now();
    for (let written = 0; written < TOTAL_BYTES; written += CHUNK_BYTES) await writable.write(chunk);
    await writable.close();
    return `OK: ${TOTAL_BYTES >> 20} MB written, ${mbps(TOTAL_BYTES, performance.now() - start)}`;
}

async function testOpfs(): Promise<string> {
    const storage = navigator.storage as StorageManager | undefined;
    if (!storage || typeof storage.getDirectory !== "function") return "NOT AVAILABLE (navigator.storage.getDirectory is missing)";
    const root = await storage.getDirectory();
    const handle = await root.getFileHandle("okcam_probe_opfs.bin", { create: true });
    const writable = await handle.createWritable();
    const chunk = makeChunk();
    const start = performance.now();
    for (let written = 0; written < TOTAL_BYTES; written += CHUNK_BYTES) await writable.write(chunk);
    await writable.close();
    const ms = performance.now() - start;
    await root.removeEntry("okcam_probe_opfs.bin");
    return `OK (private storage, not visible to the user): ${TOTAL_BYTES >> 20} MB, ${mbps(TOTAL_BYTES, ms)}`;
}

const FileWriteProbe: React.FC = () => {
    const [lines, setLines] = React.useState<string[]>([]);
    const [busy, setBusy] = React.useState(false);

    const run = async () => {
        setBusy(true);
        const out: string[] = [
            `secure context: ${window.isSecureContext}, origin: ${window.location.origin}`,
        ];
        setLines([...out]);
        const tests: [string, () => Promise<string>][] = [
            ["<a download>", testDownload],
            ["showSaveFilePicker", testPicker],
            ["OPFS", testOpfs],
        ];
        for (const [name, fn] of tests) {
            try {
                out.push(`${name}: ${await fn()}`);
            } catch (e) {
                out.push(`${name}: FAILED (${e instanceof Error ? `${e.name}: ${e.message}` : String(e)})`);
            }
            setLines([...out]);
        }
        setBusy(false);
    };

    return (
        <div style={{ position: "fixed", left: 8, bottom: 8, zIndex: 100, font: "12px monospace", color: "#ddd", maxWidth: "60vw" }}>
            <button onClick={run} disabled={busy} style={{ padding: "4px 8px" }}>
                {busy ? "Testing..." : "File write test"}
            </button>
            {lines.length > 0 && (
                <div style={{ background: "rgba(0,0,0,0.85)", padding: 8, marginTop: 4, whiteSpace: "pre-wrap" }}>
                    {lines.map((l, i) => (
                        <div key={i}>{l}</div>
                    ))}
                </div>
            )}
        </div>
    );
};

export default FileWriteProbe;
