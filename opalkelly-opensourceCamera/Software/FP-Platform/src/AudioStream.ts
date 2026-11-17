/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Capture of the 64-channel PDM microphone stream (XEM8310 / DARWIN), TypeScript counterpart of
// Software/Cxx/src/AudioStream.h. No DOM / FrontPanel dependency here: the byte source (IAudioPipe)
// and the file destination (AudioFileSink) are interfaces, so this file runs unchanged in Node tests.
//
// Packet format (4096 bytes, little-endian 32-bit words), see Gateware/XEM8310-AU25P/Microphones/rtl/audio_packetizer.v:
//   0 magic "MIC1" | 1 seq | 2,3 timestamp of the first frame | 4 nsamp (bits 15:0) + flags (bit 16 = packets lost before)
//   5 video frame count | 6,7 timestamp of the last video frame start | 8.. nsamp frames x 64 ch x 24 bit | zero padding
// A sample frame (192 bytes) is exactly one interleaved frame of a 64-channel 24-bit WAV.

export const AUDIO_CHANNELS = 64;
export const AUDIO_FRAME_BYTES = AUDIO_CHANNELS * 3; // 192
export const AUDIO_PACKET_BYTES = 4096;
export const AUDIO_HEADER_BYTES = 32;
export const AUDIO_MAGIC = 0x4d494331; // "MIC1"
export const AUDIO_PIPE_ADDRESS = 0xa1;
export const AUDIO_BLOCK_SIZE = 1024; // must equal BLOCK_WORDS * 4 in the gateware (256 words)
export const AUDIO_TICKS_PER_FRAME = 528; // 22 clk per PDM period x 24 (decimation)
export const AUDIO_TICK_HZ = 99.96e6;
/** Recording writes in chunks of this size (one write per MiB, not one per 4 KiB packet) and never waits for the disk. */
export const AUDIO_REC_CHUNK_BYTES = 1 << 20;
/** More than this waiting for the disk and the recording is stopped (the live display and the acquisition go on). */
export const AUDIO_REC_MAX_QUEUED_BYTES = 64 << 20;
export const AUDIO_CATCHUP_RING_PACKETS = 200; // ~22 ms of backlog in the DDR4 ring: the live display skips ahead
export const AUDIO_PEAK_BLOCK = 8; // frames per min/max block kept for the display

export interface AudioPacketInfo {
    seq: number;
    ts: number; // timestamp (audio clock ticks) of the first frame of the packet
    nsamp: number;
    lostBefore: boolean;
    videoFrames: number;
    videoTs: number; // timestamp of the last video frame start
    payload: Uint8Array; // nsamp * AUDIO_FRAME_BYTES bytes
}

/** Parses one 4096-byte packet. Returns null if the magic or the size is wrong. */
export function parseAudioPacket(p: Uint8Array): AudioPacketInfo | null {
    if (p.byteLength < AUDIO_PACKET_BYTES) return null;
    const v = new DataView(p.buffer, p.byteOffset, p.byteLength);
    if (v.getUint32(0, true) !== AUDIO_MAGIC) return null;
    const nf = v.getUint32(16, true);
    const nsamp = nf & 0xffff;
    if (nsamp === 0 || AUDIO_HEADER_BYTES + nsamp * AUDIO_FRAME_BYTES > AUDIO_PACKET_BYTES) return null;
    return {
        seq: v.getUint32(4, true),
        ts: v.getUint32(8, true) + v.getUint32(12, true) * 4294967296,
        nsamp,
        lostBefore: ((nf >>> 16) & 1) !== 0,
        videoFrames: v.getUint32(20, true),
        videoTs: v.getUint32(24, true) + v.getUint32(28, true) * 4294967296,
        payload: p.subarray(AUDIO_HEADER_BYTES, AUDIO_HEADER_BYTES + nsamp * AUDIO_FRAME_BYTES),
    };
}

/** Byte source: the FrontPanel block pipe 0xA1, or a test double. */
export interface IAudioPipe {
    /** Number of 32-bit words currently waiting in the gateware FIFO (WireOut 0x23). */
    available(): Promise<number>;
    /** Packets held in the gateware DDR4 ring after the last available() (undefined without the ring). */
    readonly ringUsed?: number;
    /** Maximum of ringUsed since the last reset; 0 until the first packet has been written to the ring. */
    readonly ringPeak?: number;
    /** Fills `buffer` completely (a multiple of AUDIO_PACKET_BYTES) or throws. */
    read(buffer: Uint8Array): Promise<void>;
}

/** Where recorded files go (the OPFS in the browser, a plain file in the Node tests). */
export interface AudioFileSink {
    write(data: Uint8Array): Promise<void>;
    overwrite(offset: number, data: Uint8Array): Promise<void>;
    close(): Promise<void>;
}
export type AudioSinkFactory = (fileName: string) => Promise<AudioFileSink>;

/** RF64 header (valid beyond 4 GiB): 64-channel 24-bit PCM, WAVE_FORMAT_EXTENSIBLE. 104 bytes. */
export function buildWavHeader(dataBytes: number, sampleRate: number): Uint8Array {
    const h = new Uint8Array(104);
    const v = new DataView(h.buffer);
    const str = (o: number, s: string) => {
        for (let i = 0; i < s.length; i++) h[o + i] = s.charCodeAt(i);
    };
    const u64 = (o: number, x: number) => {
        v.setUint32(o, x % 4294967296, true);
        v.setUint32(o + 4, Math.floor(x / 4294967296), true);
    };
    str(0, "RF64");
    v.setUint32(4, 0xffffffff, true);
    str(8, "WAVE");
    str(12, "ds64");
    v.setUint32(16, 28, true);
    u64(20, 4 + 36 + 48 + 8 + dataBytes); // RIFF size
    u64(28, dataBytes);
    u64(36, dataBytes / AUDIO_FRAME_BYTES); // sample count
    v.setUint32(44, 0, true);
    str(48, "fmt ");
    v.setUint32(52, 40, true);
    v.setUint16(56, 0xfffe, true); // WAVE_FORMAT_EXTENSIBLE
    v.setUint16(58, AUDIO_CHANNELS, true);
    v.setUint32(60, sampleRate, true);
    v.setUint32(64, sampleRate * AUDIO_FRAME_BYTES, true);
    v.setUint16(68, AUDIO_FRAME_BYTES, true);
    v.setUint16(70, 24, true);
    v.setUint16(72, 22, true); // cbSize
    v.setUint16(74, 24, true); // valid bits
    v.setUint32(76, 0, true); // channel mask
    h.set([1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71], 80); // KSDATAFORMAT_SUBTYPE_PCM
    str(96, "data");
    v.setUint32(100, 0xffffffff, true);
    return h;
}

export interface AudioStats {
    packets: number; // valid packets received
    lostPackets: number; // inferred from gaps in `seq`
    badBlocks: number; // invalid packets (stream resynchronised)
    frames: number; // sample frames received (lost ones excluded)
    lastSeq: number;
    recordedFrames: number; // frames written to the current recording (silence included)
    segments: number; // closed segments of the current recording
    maxGapMs: number; // longest time between two pump() calls (host stalls; the gateware FIFO holds ~3.6 ms)
    longGaps: number; // number of gaps longer than 3 ms
    readMs: number; // time spent waiting on / reading the pipe (USB), cumulative
    decodeMs: number; // time spent decoding and recording packets, cumulative
    skippedPackets: number; // packets read but not decoded (catch-up mode)
    elapsedMs: number; // wall time since start()
}

export interface AudioRecorderOptions {
    tickHz?: number;
    ringFrames?: number; // frames kept for the display
    segmentSeconds?: number; // a recording is cut into files of this duration
    maxPacketsPerRead?: number;
    /** Keep the raw samples of the last `ringFrames` frames (recent()); the display only needs the min/max peaks. Default true. */
    keepRaw?: boolean;
    /**
     * When not recording and the gateware FIFO is nearly full (the PC is behind), read the packets but only decode the
     * last one of each read, so the live display catches up instead of showing old audio. Default false.
     */
    catchUp?: boolean;
    /** Runs a device operation on the (serialised) device queue. Default: run directly. */
    runOnDevice?: (task: () => Promise<void>) => Promise<void>;
    /** Called once a segment (wav + csv) is closed, e.g. to export it. */
    onSegmentClosed?: (wavName: string, csvName: string) => Promise<void>;
}

/** One output file (WAV + CSV) being written. */
interface Segment {
    wavName: string;
    sink: AudioFileSink;
    writing: Promise<void>; // chain of the pending chunk writes (never awaited by the acquisition)
    queuedBytes: number;
    chunk: Uint8Array;
    chunkLen: number;
    frames: number;
    csv: string[];
}

interface Recording {
    session: string;
    factory: AudioSinkFactory;
    segment: number;
    current: Segment | null;
    finalizing: Promise<void>[]; // closed segments being finalised in the background
    failed: string | null;
}

export class AudioRecorder {
    private readonly _pipe: IAudioPipe;
    private readonly _sampleRate: number;
    private readonly _ringFrames: number;
    private readonly _ring: Int32Array; // interleaved frames, 64 values each (empty without keepRaw)
    private readonly _keepRaw: boolean;
    private readonly _catchUp: boolean;
    private _startTime = 0;
    private _ringHead = 0;
    private _ringFill = 0;
    // Per-channel min/max of blocks of AUDIO_PEAK_BLOCK frames: the display reads these instead of the raw
    // samples, so drawing costs ~100x less main-thread time (the gateware FIFO only holds ~3.6 ms of audio).
    private readonly _peakBlocks: number;
    private readonly _peakMin: Int32Array;
    private readonly _peakMax: Int32Array;
    private _peakHead = 0;
    private _peakFill = 0;
    private _accCount = 0;
    private readonly _accMin = new Int32Array(AUDIO_CHANNELS);
    private readonly _accMax = new Int32Array(AUDIO_CHANNELS);
    private readonly _segmentFrames: number;
    private readonly _maxPackets: number;
    private readonly _runOnDevice: (task: () => Promise<void>) => Promise<void>;
    private readonly _onSegmentClosed?: (wavName: string, csvName: string) => Promise<void>;

    private _expectSeq = false;
    private _lastPump = 0;
    private _nextSeq = 0;
    private _rec: Recording | null = null;
    private _stats: AudioStats = AudioRecorder.emptyStats();
    private _buffer: Uint8Array;
    private _loopRunning = false;
    private _stopRequested = false;
    private _error: string | null = null;
    private _recordError: string | null = null;

    constructor(pipe: IAudioPipe, options: AudioRecorderOptions = {}) {
        this._pipe = pipe;
        const tickHz = options.tickHz ?? AUDIO_TICK_HZ;
        this._sampleRate = Math.round(tickHz / AUDIO_TICKS_PER_FRAME);
        this._ringFrames = options.ringFrames ?? 65536;
        this._keepRaw = options.keepRaw ?? true;
        this._catchUp = options.catchUp ?? false;
        this._ring = new Int32Array(this._keepRaw ? this._ringFrames * AUDIO_CHANNELS : 0);
        this._peakBlocks = Math.floor(this._ringFrames / AUDIO_PEAK_BLOCK);
        this._peakMin = new Int32Array(this._peakBlocks * AUDIO_CHANNELS);
        this._peakMax = new Int32Array(this._peakBlocks * AUDIO_CHANNELS);
        this._segmentFrames = Math.max(1, Math.round((options.segmentSeconds ?? 60) * this._sampleRate));
        this._maxPackets = options.maxPacketsPerRead ?? 32; // the gateware FIFO holds 32 packets
        this._runOnDevice = options.runOnDevice ?? ((task) => task());
        this._onSegmentClosed = options.onSegmentClosed;
        this._buffer = new Uint8Array(AUDIO_PACKET_BYTES * this._maxPackets);
    }

    private static emptyStats(): AudioStats {
        return { packets: 0, lostPackets: 0, badBlocks: 0, frames: 0, lastSeq: 0, recordedFrames: 0, segments: 0, maxGapMs: 0, longGaps: 0, readMs: 0, decodeMs: 0, skippedPackets: 0, elapsedMs: 0 };
    }

    get sampleRate(): number {
        return this._sampleRate;
    }
    get stats(): AudioStats {
        return { ...this._stats, elapsedMs: this._startTime > 0 ? performance.now() - this._startTime : 0 };
    }
    get error(): string | null {
        return this._error;
    }
    get recording(): boolean {
        return this._rec !== null && this._rec.failed === null;
    }
    /** Why the recording stopped by itself (storage quota, disk too slow...), null otherwise. */
    get recordError(): string | null {
        return this._recordError;
    }

    /**
     * Reads what is currently waiting in the gateware FIFO (whole packets only) and processes it. Never
     * waits for data: it is meant to be called between slices of a video transfer, and from a timer.
     * Must be called on the device queue (see `runOnDevice`).
     */
    async pump(): Promise<number> {
        const now = performance.now();
        if (this._lastPump > 0) {
            const gap = now - this._lastPump;
            if (gap > this._stats.maxGapMs) this._stats.maxGapMs = gap;
            if (gap > 3) this._stats.longGaps++;
        }
        this._lastPump = now;
        const t0 = performance.now();
        const words = await this._pipe.available();
        let packets = Math.floor(words / (AUDIO_PACKET_BYTES / 4));
        if (packets > this._maxPackets) packets = this._maxPackets;
        if (packets === 0) {
            this._stats.readMs += performance.now() - t0;
            return 0;
        }
        const chunk = this._buffer.subarray(0, packets * AUDIO_PACKET_BYTES);
        await this._pipe.read(chunk);
        const t1 = performance.now();
        this._stats.readMs += t1 - t0;
        // The PC is behind: with the DDR4 ring, a backlog of several dozen ms is queued in it (the gateware FIFO in front of it
        // is always full then, which says nothing); without the ring, a nearly full FIFO means the same. Without a recording,
        // only the newest packet of each read is decoded so that the live display shows the present.
        const peak = this._pipe.ringPeak;
        const behindNow =
            peak !== undefined && peak > 0
                ? (this._pipe.ringUsed ?? 0) >= AUDIO_CATCHUP_RING_PACKETS
                : words >= (this._maxPackets - 4) * (AUDIO_PACKET_BYTES / 4);
        const behind = this._catchUp && !this._rec && behindNow;
        for (let i = 0; i < packets; i++) {
            const pkt = chunk.subarray(i * AUDIO_PACKET_BYTES, (i + 1) * AUDIO_PACKET_BYTES);
            if (behind && i < packets - 1) this.skipPacket(pkt);
            else await this.handle(pkt);
        }
        this._stats.decodeMs += performance.now() - t1;
        return packets;
    }

    /** Starts the pump loop on the device queue (it also runs between slices of video transfers). */
    start(): void {
        if (this._loopRunning) return;
        this._loopRunning = true;
        this._stopRequested = false;
        this._error = null;
        this._expectSeq = false;
        this._lastPump = 0;
        this._startTime = performance.now();
        this._stats.maxGapMs = 0;
        this._stats.longGaps = 0;
        this._stats.readMs = 0;
        this._stats.decodeMs = 0;
        this._stats.skippedPackets = 0;
        void (async () => {
            try {
                while (!this._stopRequested) {
                    await this._runOnDevice(async () => {
                        if ((await this.pump()) === 0) await this.pumpBlocking();
                    });
                }
            } catch (e) {
                this._error = e instanceof Error ? e.message : String(e);
            }
            this._loopRunning = false;
        })();
    }

    /** Stops the timer-driven pump; resolves when it has finished. Close the recording separately. */
    async stop(): Promise<void> {
        this._stopRequested = true;
        while (this._loopRunning) await new Promise((r) => setTimeout(r, 5));
    }

    /** Starts writing segments named `<session>_<n>.wav` / `.csv` through `factory`. */
    startRecording(session: string, factory: AudioSinkFactory): void {
        // a previous recording that failed was never finalised: release its file
        void this._rec?.current?.sink.close().catch(() => undefined);
        this._rec = { session, factory, segment: 0, current: null, finalizing: [], failed: null };
        this._recordError = null;
        this._stats.recordedFrames = 0;
        this._stats.segments = 0;
    }

    /** Closes the current segment (waits for pending writes) and stops recording. */
    async stopRecording(): Promise<void> {
        const rec = this._rec;
        if (!rec) return;
        this._rec = null; // no more packets are appended
        if (rec.current) {
            rec.finalizing.push(this.finalizeSegment(rec, rec.current));
            rec.current = null;
        }
        await Promise.all(rec.finalizing);
    }

    /** The last up-to-`maxFrames` samples of one channel (24-bit, sign-extended), oldest first. */
    recent(channel: number, maxFrames: number): Int32Array {
        if (!this._keepRaw) return new Int32Array(0);
        const n = Math.min(maxFrames, this._ringFill);
        const out = new Int32Array(n);
        const start = (this._ringHead + this._ringFrames - n) % this._ringFrames;
        for (let i = 0; i < n; i++) out[i] = this._ring[((start + i) % this._ringFrames) * AUDIO_CHANNELS + channel];
        return out;
    }

    /**
     * Min/max of the last up-to-`maxBlocks` blocks (AUDIO_PEAK_BLOCK frames each) of one channel, oldest first,
     * written into the caller's arrays (no allocation). Returns the number of blocks written.
     */
    recentPeaks(channel: number, maxBlocks: number, outMin: Int32Array, outMax: Int32Array): number {
        const n = Math.min(maxBlocks, this._peakFill, outMin.length, outMax.length);
        const start = (this._peakHead + this._peakBlocks - n) % this._peakBlocks;
        for (let i = 0; i < n; i++) {
            const o = ((start + i) % this._peakBlocks) * AUDIO_CHANNELS + channel;
            outMin[i] = this._peakMin[o];
            outMax[i] = this._peakMax[o];
        }
        return n;
    }

    get peakBlocks(): number {
        return this._peakBlocks;
    }

    /**
     * Waits for ONE packet (a blocking read, ~0.11 ms at the nominal rate) and processes it. Used instead of
     * a timer when the FIFO is empty: a JS timer has a >= 4 ms latency, longer than the ~3.6 ms the gateware
     * FIFO can hold. The microphones must stay enabled while this runs.
     */
    async pumpBlocking(): Promise<number> {
        const one = this._buffer.subarray(0, AUDIO_PACKET_BYTES);
        const t0 = performance.now();
        await this._pipe.read(one);
        const t1 = performance.now();
        this._stats.readMs += t1 - t0;
        await this.handle(one);
        this._stats.decodeMs += performance.now() - t1;
        return 1;
    }

    // ---- internals -----------------------------------------------------------------------------

    private async handle(p: Uint8Array): Promise<void> {
        const h = parseAudioPacket(p);
        if (!h) {
            this._stats.badBlocks++;
            return;
        }
        let lostFrames = 0;
        if (this._expectSeq && h.seq !== this._nextSeq) {
            const gap = (h.seq - this._nextSeq) >>> 0;
            this._stats.lostPackets += gap;
            lostFrames = gap * h.nsamp;
        }
        this._expectSeq = true;
        this._nextSeq = (h.seq + 1) >>> 0;

        // decode once: raw ring (optional) and per-block min/max peaks for the display
        const keep = this._keepRaw;
        const pay = h.payload;
        const accMin = this._accMin;
        const accMax = this._accMax;
        for (let i = 0; i < h.nsamp; i++) {
            const f = i * AUDIO_FRAME_BYTES;
            const base = this._ringHead * AUDIO_CHANNELS;
            const first = this._accCount === 0;
            for (let c = 0; c < AUDIO_CHANNELS; c++) {
                const o = f + c * 3;
                const v = ((pay[o] | (pay[o + 1] << 8) | (pay[o + 2] << 16)) << 8) >> 8;
                if (keep) this._ring[base + c] = v;
                if (first || v < accMin[c]) accMin[c] = v;
                if (first || v > accMax[c]) accMax[c] = v;
            }
            if (keep) {
                this._ringHead = (this._ringHead + 1) % this._ringFrames;
                if (this._ringFill < this._ringFrames) this._ringFill++;
            }
            if (++this._accCount >= AUDIO_PEAK_BLOCK) {
                const o = this._peakHead * AUDIO_CHANNELS;
                this._peakMin.set(accMin, o);
                this._peakMax.set(accMax, o);
                this._peakHead = (this._peakHead + 1) % this._peakBlocks;
                if (this._peakFill < this._peakBlocks) this._peakFill++;
                this._accCount = 0;
            }
        }

        if (this._rec) await this.record(h, lostFrames);

        this._stats.packets++;
        this._stats.frames += h.nsamp;
        this._stats.lastSeq = h.seq;
    }

    /** Counts a packet (sequence / loss tracking) without decoding its samples (catch-up mode). */
    private skipPacket(p: Uint8Array): void {
        const h = parseAudioPacket(p);
        if (!h) {
            this._stats.badBlocks++;
            return;
        }
        if (this._expectSeq && h.seq !== this._nextSeq) this._stats.lostPackets += (h.seq - this._nextSeq) >>> 0;
        this._expectSeq = true;
        this._nextSeq = (h.seq + 1) >>> 0;
        this._stats.packets++;
        this._stats.frames += h.nsamp;
        this._stats.skippedPackets++;
        this._stats.lastSeq = h.seq;
    }

    private async record(h: AudioPacketInfo, lostFrames: number): Promise<void> {
        const rec = this._rec;
        if (!rec || rec.failed) return;
        try {
            if (!rec.current) rec.current = await this.openSegment(rec);
            const seg = rec.current;
            if (lostFrames > 0) {
                // keep the file position proportional to time: silence in place of the lost packets
                const zeros = new Uint8Array(AUDIO_FRAME_BYTES * 1024);
                let left = lostFrames;
                while (left > 0) {
                    const n = Math.min(left, 1024);
                    this.append(rec, seg, zeros.subarray(0, n * AUDIO_FRAME_BYTES));
                    left -= n;
                }
                seg.frames += lostFrames;
                this._stats.recordedFrames += lostFrames;
            }
            seg.csv.push(`${h.seq},${h.ts},${seg.frames},${h.videoFrames},${h.videoTs},${h.lostBefore ? 1 : 0}`);
            this.append(rec, seg, h.payload);
            seg.frames += h.nsamp;
            this._stats.recordedFrames += h.nsamp;
            if (!rec.failed && seg.frames >= this._segmentFrames) {
                // Close in the background: the acquisition must not wait for the disk
                rec.current = null;
                rec.segment++;
                rec.finalizing.push(this.finalizeSegment(rec, seg));
            }
        } catch (e) {
            this.failRecording(rec, e);
        }
    }

    private async openSegment(rec: Recording): Promise<Segment> {
        const wavName = `${rec.session}_${String(rec.segment).padStart(3, "0")}.wav`;
        const seg: Segment = {
            wavName,
            sink: await rec.factory(wavName),
            writing: Promise.resolve(),
            queuedBytes: 0,
            chunk: new Uint8Array(AUDIO_REC_CHUNK_BYTES),
            chunkLen: 0,
            frames: 0,
            csv: ["seq,ts,first_frame,video_frames,video_ts,lost_before"],
        };
        this.append(rec, seg, buildWavHeader(0, this._sampleRate)); // placeholder, rewritten at the end
        return seg;
    }

    /** Copies bytes into the current chunk; a full chunk is written in the background (never awaited here). */
    private append(rec: Recording, seg: Segment, data: Uint8Array): void {
        let off = 0;
        while (off < data.byteLength) {
            const n = Math.min(data.byteLength - off, seg.chunk.byteLength - seg.chunkLen);
            seg.chunk.set(data.subarray(off, off + n), seg.chunkLen);
            seg.chunkLen += n;
            off += n;
            if (seg.chunkLen === seg.chunk.byteLength) this.flushChunk(rec, seg);
        }
    }

    private flushChunk(rec: Recording, seg: Segment): void {
        if (seg.chunkLen === 0) return;
        const buf = seg.chunk.subarray(0, seg.chunkLen);
        seg.chunk = new Uint8Array(AUDIO_REC_CHUNK_BYTES);
        seg.chunkLen = 0;
        seg.queuedBytes += buf.byteLength;
        if (seg.queuedBytes > AUDIO_REC_MAX_QUEUED_BYTES) {
            this.failRecording(rec, new Error("disk write queue overflow: the storage cannot keep up with the audio"));
            return;
        }
        seg.writing = seg.writing.then(async () => {
            if (rec.failed) return;
            try {
                await seg.sink.write(buf);
            } catch (e) {
                this.failRecording(rec, e);
            } finally {
                seg.queuedBytes -= buf.byteLength;
            }
        });
    }

    private failRecording(rec: Recording, e: unknown): void {
        if (rec.failed) return;
        rec.failed = e instanceof Error ? `${e.name}: ${e.message}` : String(e);
        this._recordError = rec.failed;
    }

    /** Writes the end of a segment (header sizes, CSV) and exports it. Never throws: errors stop the recording. */
    private async finalizeSegment(rec: Recording, seg: Segment): Promise<void> {
        try {
            this.flushChunk(rec, seg);
            await seg.writing;
            if (rec.failed) {
                await seg.sink.close().catch(() => undefined);
                return;
            }
            await seg.sink.overwrite(0, buildWavHeader(seg.frames * AUDIO_FRAME_BYTES, this._sampleRate));
            await seg.sink.close();
            const csvName = seg.wavName.replace(/\.wav$/, ".csv");
            const csv = await rec.factory(csvName);
            await csv.write(new TextEncoder().encode(seg.csv.join("\n") + "\n"));
            await csv.close();
            this._stats.segments++;
            if (this._onSegmentClosed) await this._onSegmentClosed(seg.wavName, csvName);
        } catch (e) {
            this.failRecording(rec, e);
        }
    }
}
