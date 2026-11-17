// Hardware-free test of src/AudioStream.ts. Run: npm run test:audio
import * as assert from "assert";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import { execFileSync } from "child_process";

import {
    AUDIO_FRAME_BYTES,
    AUDIO_MAGIC,
    AUDIO_PEAK_BLOCK,
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


    // display peaks: must equal the min/max of the raw samples of each block of AUDIO_PEAK_BLOCK frames
    {
        const blocksWanted = 40;
        const pmin = new Int32Array(blocksWanted);
        const pmax = new Int32Array(blocksWanted);
        for (const ch of [0, 5, 63]) {
            const n = rec.recentPeaks(ch, blocksWanted, pmin, pmax);
            assert.strictEqual(n, blocksWanted);
            // blocks are aligned on the received frame count; the last (partial) block is not published yet
            const tail = st.frames % AUDIO_PEAK_BLOCK;
            const raw = rec.recent(ch, blocksWanted * AUDIO_PEAK_BLOCK + tail);
            for (let b = 0; b < n; b++) {
                const slice = raw.subarray(b * AUDIO_PEAK_BLOCK, (b + 1) * AUDIO_PEAK_BLOCK);
                assert.strictEqual(pmin[b], Math.min(...slice), `peak min ch ${ch} block ${b}`);
                assert.strictEqual(pmax[b], Math.max(...slice), `peak max ch ${ch} block ${b}`);
            }
        }
    }

    // pumpBlocking: waits for exactly one packet
    {
        const p2 = new FakePipe();
        const r2 = new AudioRecorder(p2, { ringFrames: 1000 });
        p2.queue.push(makePacket(0, 0, false));
        assert.strictEqual(await r2.pumpBlocking(), 1);
        assert.strictEqual(r2.stats.packets, 1);
    }


    // catch-up mode: PC behind (FIFO nearly full) and not recording -> only the newest packet of each read is decoded,
    // sequence / loss tracking stays exact
    {
        const p3 = new FakePipe();
        const lostSet = new Set([3, 4, 20]);
        for (let q = 0; q < 40; q++) if (!lostSet.has(q)) p3.queue.push(makePacket(q, 0, false));
        const r3 = new AudioRecorder(p3, { ringFrames: 5000, catchUp: true, keepRaw: false });
        while ((await r3.pump()) > 0) {
            /* drain */
        }
        const st3 = r3.stats;
        assert.strictEqual(st3.packets, 40 - lostSet.size);
        assert.strictEqual(st3.lostPackets, lostSet.size);
        assert.ok(st3.skippedPackets > 0, "behind: some packets must be skipped");
        assert.ok(st3.skippedPackets < st3.packets);
        assert.strictEqual(r3.recent(0, 10).length, 0); // no raw ring without keepRaw
        const pk = new Int32Array(8);
        assert.ok(r3.recentPeaks(0, 8, pk, new Int32Array(8)) > 0);
    }


    // catch-up with the DDR4 ring: the FIFO being full says nothing, only the ring backlog counts
    {
        class RingPipe extends FakePipe {
            ringUsed = 0;
            ringPeak = 1;
        }
        const mk = (used: number) => {
            const rp = new RingPipe();
            rp.ringUsed = used;
            for (let q = 0; q < 40; q++) rp.queue.push(makePacket(q, 0, false));
            return rp;
        };
        const idle = new AudioRecorder(mk(5), { ringFrames: 2000, catchUp: true, keepRaw: false });
        while ((await idle.pump()) > 0) {
            /* drain */
        }
        assert.strictEqual(idle.stats.skippedPackets, 0, "no ring backlog: nothing skipped even with a full FIFO");
        assert.strictEqual(idle.stats.packets, 40);
        const behindRec = new AudioRecorder(mk(500), { ringFrames: 2000, catchUp: true, keepRaw: false });
        while ((await behindRec.pump()) > 0) {
            /* drain */
        }
        assert.ok(behindRec.stats.skippedPackets > 0, "large ring backlog: packets skipped");
    }


    // recording is batched (one write per MiB, not per packet) and never waits for the disk
    {
        let writes = 0;
        class CountingSink extends FileSink {
            async write(data: Uint8Array) {
                writes++;
                return super.write(data);
            }
        }
        const pb = new FakePipe();
        const NPK = 600;
        for (let q = 0; q < NPK; q++) pb.queue.push(makePacket(q, 0, false));
        const rb = new AudioRecorder(pb, { ringFrames: 2000, keepRaw: false, segmentSeconds: 600 });
        const dir2 = fs.mkdtempSync(path.join(os.tmpdir(), "okaudio-b-"));
        rb.startRecording("okcam_batch", async (name) => new CountingSink(path.join(dir2, name)));
        while ((await rb.pump()) > 0) {
            /* drain */
        }
        await rb.stopRecording();
        assert.ok(writes < NPK / 20, `batched writes expected, got ${writes} writes for ${NPK} packets`);
        const f0 = fs.readFileSync(path.join(dir2, "okcam_batch_000.wav"));
        assert.strictEqual(f0.length, 104 + NPK * 21 * AUDIO_FRAME_BYTES);
        fs.rmSync(dir2, { recursive: true, force: true });
    }

    // a failing disk / exceeded quota stops the recording, never the acquisition, and nothing throws out of pump()
    {
        class QuotaSink extends FileSink {
            private _n = 0;
            async write(data: Uint8Array) {
                if (++this._n > 2) throw new Error("The operation failed because it would cause the application to exceed its storage quota.");
                return super.write(data);
            }
        }
        const pq = new FakePipe();
        for (let q = 0; q < 800; q++) pq.queue.push(makePacket(q, 0, false));
        const rq = new AudioRecorder(pq, { ringFrames: 2000, keepRaw: false, segmentSeconds: 600 });
        const dir3 = fs.mkdtempSync(path.join(os.tmpdir(), "okaudio-q-"));
        rq.startRecording("okcam_quota", async (name) => new QuotaSink(path.join(dir3, name)));
        while ((await rq.pump()) > 0) {
            /* drain */
        }
        assert.strictEqual(rq.stats.packets, 800, "the acquisition continued");
        await new Promise((r) => setTimeout(r, 20));
        assert.ok(rq.recordError && rq.recordError.includes("storage quota"), `recordError: ${rq.recordError}`);
        assert.strictEqual(rq.recording, false);
        await rq.stopRecording(); // must not throw
        fs.rmSync(dir3, { recursive: true, force: true });
    }

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
