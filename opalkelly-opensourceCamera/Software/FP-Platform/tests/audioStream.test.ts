// Hardware-free test of src/AudioStream.ts. Run: npm run test:audio
import * as assert from "assert";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import { execFileSync } from "child_process";

import {
    AUDIO_FRAME_BYTES,
    AUDIO_MAGIC,
    AUDIO_PACKET_BYTES,
    AudioFileSink,
    AudioRecorder,
    IAudioPipe,
    parseAudioPacket,
} from "../src/AudioStream";

const NS = 21;
const sampleValue = (g: number, c: number): number => ((g * 977 + c * 131) % 16000000) - 8000000;

function makePacket(seq: number, vframes: number, lostBefore: boolean): Uint8Array {
    const p = new Uint8Array(AUDIO_PACKET_BYTES);
    const v = new DataView(p.buffer);
    v.setUint32(0, AUDIO_MAGIC, true);
    v.setUint32(4, seq, true);
    const ts = 1000000 + seq * NS * 528;
    v.setUint32(8, ts % 4294967296, true);
    v.setUint32(12, Math.floor(ts / 4294967296), true);
    v.setUint32(16, NS | (lostBefore ? 1 << 16 : 0), true);
    v.setUint32(20, vframes, true);
    for (let i = 0; i < NS; i++)
        for (let c = 0; c < 64; c++) {
            const x = sampleValue(seq * NS + i, c) & 0xffffff;
            const o = 32 + i * AUDIO_FRAME_BYTES + c * 3;
            p[o] = x & 0xff;
            p[o + 1] = (x >> 8) & 0xff;
            p[o + 2] = (x >> 16) & 0xff;
        }
    return p;
}

class FakePipe implements IAudioPipe {
    queue: Uint8Array[] = [];
    async available(): Promise<number> {
        return (this.queue.length * AUDIO_PACKET_BYTES) / 4;
    }
    async read(buffer: Uint8Array): Promise<void> {
        const n = buffer.byteLength / AUDIO_PACKET_BYTES;
        for (let i = 0; i < n; i++) buffer.set(this.queue.shift()!, i * AUDIO_PACKET_BYTES);
    }
}

class FileSink implements AudioFileSink {
    private readonly _fd: number;
    private _pos = 0;
    constructor(file: string) {
        this._fd = fs.openSync(file, "w+");
    }
    async write(data: Uint8Array) {
        fs.writeSync(this._fd, data, 0, data.byteLength, this._pos);
        this._pos += data.byteLength;
    }
    async overwrite(offset: number, data: Uint8Array) {
        fs.writeSync(this._fd, data, 0, data.byteLength, offset);
    }
    async close() {
        fs.closeSync(this._fd);
    }
}

async function main() {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "okaudio-"));
    const closed: string[] = [];
    const pipe = new FakePipe();
    const lost = new Set([10, 11, 50, 51, 52]);
    const TOTAL = 400;
    let nextVideo = 0;
    for (let s = 0; s < TOTAL; s++) {
        if (lost.has(s)) continue;
        pipe.queue.push(makePacket(s, nextVideo++ / 10, lost.has(s - 1)));
    }

    // parse sanity
    const p0 = parseAudioPacket(makePacket(7, 3, true))!;
    assert.strictEqual(p0.seq, 7);
    assert.strictEqual(p0.nsamp, NS);
    assert.strictEqual(p0.lostBefore, true);
    assert.strictEqual(p0.videoFrames, 3);
    assert.strictEqual(p0.ts, 1000000 + 7 * NS * 528);
    assert.strictEqual(parseAudioPacket(new Uint8Array(AUDIO_PACKET_BYTES)), null);

    const rec = new AudioRecorder(pipe, {
        tickHz: 99.96e6,
        ringFrames: 5000,
        segmentSeconds: 0.02, // ~3786 frames = 180.3 packets -> segments of 181 packets
        onSegmentClosed: async (wav) => {
            closed.push(wav);
        },
    });
    assert.strictEqual(rec.sampleRate, 189318);
    rec.startRecording("okcam_test", async (name) => new FileSink(path.join(dir, name)));
    while ((await rec.pump()) > 0) {
        /* drain */
    }
    await rec.stopRecording();

    const st = rec.stats;
    console.log("stats", JSON.stringify(st), "segments", closed.join(","));
    assert.strictEqual(st.packets, TOTAL - lost.size);
    assert.strictEqual(st.lostPackets, lost.size);
    assert.strictEqual(st.badBlocks, 0);
    assert.strictEqual(st.recordedFrames, TOTAL * NS); // lost packets replaced by silence: full duration

    // files: contents frame by frame across the segments
    const files = fs.readdirSync(dir).filter((f) => f.endsWith(".wav")).sort();
    assert.deepStrictEqual(files, closed.slice().sort());
    assert.ok(files.length >= 2, "expected several segments");
    let g = 0;
    for (const f of files) {
        const buf = fs.readFileSync(path.join(dir, f));
        assert.strictEqual(buf.toString("latin1", 0, 4), "RF64");
        const frames = (buf.length - 104) / AUDIO_FRAME_BYTES;
        assert.strictEqual(buf.readUInt32LE(28), frames * AUDIO_FRAME_BYTES); // ds64 data size
        assert.strictEqual(buf.readUInt32LE(60), 189318);
        for (let i = 0; i < frames; i++, g++) {
            const seq = Math.floor(g / NS);
            for (let c = 0; c < 64; c += 9) {
                const o = 104 + i * AUDIO_FRAME_BYTES + c * 3;
                let v = buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16);
                v = (v << 8) >> 8;
                const expected = lost.has(seq) ? 0 : sampleValue(g, c);
                if (v !== expected) throw new Error(`mismatch in ${f} frame ${i} (global ${g}) ch ${c}: ${v} != ${expected}`);
            }
        }
        const csv = fs.readFileSync(path.join(dir, f.replace(".wav", ".csv")), "utf8").trim().split("\n");
        assert.strictEqual(csv[0], "seq,ts,first_frame,video_frames,video_ts,lost_before");
        assert.ok(csv.length > 1);
    }
    assert.strictEqual(g, TOTAL * NS);

    // display ring: last samples of channel 5
    const recent = rec.recent(5, 100);
    assert.strictEqual(recent.length, 100);
    assert.strictEqual(recent[99], sampleValue(TOTAL * NS - 1, 5));
    assert.strictEqual(recent[0], sampleValue(TOTAL * NS - 100, 5));

    // independent reader: ffprobe if installed
    try {
        const out = execFileSync(
            "ffprobe",
            ["-v", "error", "-show_entries", "stream=codec_name,channels,sample_rate,bits_per_sample", "-of", "default=nw=1", path.join(dir, files[0])],
            { encoding: "utf8" }
        );
        console.log(out.trim().replace(/\n/g, " "));
        assert.ok(out.includes("pcm_s24le") && out.includes("channels=64"));
    } catch (e) {
        if ((e as NodeJS.ErrnoException).code !== "ENOENT") throw e;
        console.log("ffprobe not installed: skipped");
    }
    fs.rmSync(dir, { recursive: true, force: true });
    console.log("ALL AUDIO TESTS PASSED");
}

main().catch((e) => {
    console.error(e);
    process.exit(1);
});
