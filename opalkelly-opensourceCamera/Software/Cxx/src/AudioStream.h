/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AudioStream.h — capture of the 64-channel PDM microphone stream (XEM8310 / DARWIN). C++ counterpart of
// Software/FP-Platform/src/AudioStream.ts (same design, same tests).
//
// The gateware (Gateware/XEM8310-AU25P/Microphones) sends 4096-byte packets on block pipe 0xA1:
//   8 header words (magic "MIC1", seq, 64-bit timestamp of the first sample, nsamp + flags, video frame count,
//   64-bit timestamp of the last video frame start), then nsamp (21) sample frames of 64 channels x 24-bit signed
//   little-endian, then zero padding. A sample frame is exactly one interleaved frame of a 64-channel 24-bit WAV.
//
// AudioCapture does NOT own a thread: pump() must be called from the thread that talks to the device (the GUI's
// camera worker). It is called (a) between slices of every large video transfer (the gateware only buffers a few ms
// of audio in front of its DDR4 ring: see AxiStreamOverClassicDataPort::setBetweenChunksHook) and (b) from the
// worker loop when no video transfer is running.
//
// Recording never waits for the disk: packets are appended to 1 MiB chunks that a writer thread writes; segments are
// closed in the background; a disk error or a full disk stops the RECORDING, never the acquisition. Lost packets
// (gap in `seq`) are replaced by silence so that the file position stays proportional to time, and a CSV sidecar
// records, per packet, the timestamps needed to align the audio with the video.
//
// Header-only, no dependency on the FrontPanel API: the byte source is the IAudioPipe interface.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "AudioConvert.h"

namespace okcli {

constexpr int      kAudioChannels      = 64;
constexpr int      kAudioBytesPerSmp   = 3;
constexpr int      kAudioFrameBytes    = kAudioChannels * kAudioBytesPerSmp;   // 192
constexpr int      kAudioPacketWords   = 1024;
constexpr int      kAudioPacketBytes   = kAudioPacketWords * 4;                // 4096
constexpr int      kAudioHeaderWords   = 8;
constexpr uint32_t kAudioMagic         = 0x4D494331;                           // "MIC1"
constexpr int      kAudioBlockSize     = 1024;     // pipe block size in bytes (= BLOCK_WORDS * 4 in the gateware)
constexpr int      kAudioPipeAddress   = 0xA1;
constexpr int      kAudioTicksPerFrame = 528;      // 22 clk per PDM period x 24 (decimation)
constexpr double   kAudioTickHz        = 99.96e6;
constexpr int      kAudioPeakBlock     = 8;        // frames per min/max block kept for the display
constexpr std::size_t kAudioRecChunkBytes     = (std::size_t(1) << 20) / kAudioFrameBytes * kAudioFrameBytes;   // ~1 MiB per disk write, not per packet; whole frames (the converter works on frames)
constexpr std::size_t kAudioRecMaxQueuedBytes = std::size_t(64) << 20;  // more than this waiting: the recording stops
constexpr uint32_t kAudioCatchUpRingPackets   = 200;                    // ~22 ms of backlog in the DDR4 ring

struct AudioPacketInfo {
    uint32_t seq = 0;
    uint64_t ts = 0;          // timestamp (audio clock ticks) of the first frame of the packet
    uint32_t nsamp = 0;       // sample frames in the packet
    bool     lostBefore = false;   // the gateware reports dropped packets before this one
    uint32_t videoFrames = 0; // video frame starts seen
    uint64_t videoTs = 0;     // timestamp of the last video frame start
    const uint8_t* payload = nullptr;   // nsamp * kAudioFrameBytes bytes
};

inline uint32_t audioWord(const uint8_t* p, int i) {
    return uint32_t(p[4*i]) | uint32_t(p[4*i+1]) << 8 | uint32_t(p[4*i+2]) << 16 | uint32_t(p[4*i+3]) << 24;
}

// Parses one 4096-byte packet. Returns false if the magic/size is wrong.
inline bool parseAudioPacket(const uint8_t* p, AudioPacketInfo& out) {
    if (audioWord(p, 0) != kAudioMagic) return false;
    out.seq = audioWord(p, 1);
    out.ts = uint64_t(audioWord(p, 2)) | uint64_t(audioWord(p, 3)) << 32;
    const uint32_t nf = audioWord(p, 4);
    out.nsamp = nf & 0xFFFF;
    out.lostBefore = ((nf >> 16) & 1) != 0;
    out.videoFrames = audioWord(p, 5);
    out.videoTs = uint64_t(audioWord(p, 6)) | uint64_t(audioWord(p, 7)) << 32;
    if (out.nsamp == 0 || kAudioHeaderWords * 4 + out.nsamp * kAudioFrameBytes > kAudioPacketBytes) return false;
    out.payload = p + kAudioHeaderWords * 4;
    return true;
}

// RF64 header (valid beyond 4 GiB): 64-channel 24-bit PCM, WAVE_FORMAT_EXTENSIBLE. 104 bytes.
inline std::vector<uint8_t> buildWavHeader(uint64_t dataBytes, uint32_t sampleRate, int bytesPerSample = 3) {
    const uint32_t frameBytes = uint32_t(kAudioChannels * bytesPerSample);
    std::vector<uint8_t> h(104, 0);
    auto str = [&](std::size_t o, const char* s) { std::memcpy(&h[o], s, std::strlen(s)); };
    auto u16 = [&](std::size_t o, uint16_t v) { h[o] = uint8_t(v); h[o + 1] = uint8_t(v >> 8); };
    auto u32 = [&](std::size_t o, uint32_t v) { for (int i = 0; i < 4; i++) h[o + i] = uint8_t(v >> (8 * i)); };
    auto u64 = [&](std::size_t o, uint64_t v) { for (int i = 0; i < 8; i++) h[o + i] = uint8_t(v >> (8 * i)); };
    str(0, "RF64"); u32(4, 0xFFFFFFFFu); str(8, "WAVE");
    str(12, "ds64"); u32(16, 28);
    u64(20, 4 + 36 + 48 + 8 + dataBytes);        // RIFF size
    u64(28, dataBytes);
    u64(36, dataBytes / frameBytes);             // sample count
    u32(44, 0);
    str(48, "fmt "); u32(52, 40);
    u16(56, 0xFFFE); u16(58, kAudioChannels); u32(60, sampleRate); u32(64, sampleRate * frameBytes);
    u16(68, uint16_t(frameBytes)); u16(70, uint16_t(8 * bytesPerSample));
    u16(72, 22); u16(74, uint16_t(8 * bytesPerSample)); u32(76, 0);        // extension size, valid bits, channel mask
    static const uint8_t pcm[16] = {1,0,0,0, 0,0, 0x10,0, 0x80,0, 0,0xAA,0, 0x38,0x9B,0x71};  // KSDATAFORMAT_SUBTYPE_PCM
    std::memcpy(&h[80], pcm, 16);
    str(96, "data"); u32(100, 0xFFFFFFFFu);
    return h;
}

// Source of bytes (the FrontPanel block pipe, or a test double).
class IAudioPipe {
public:
    virtual ~IAudioPipe() = default;
    // Number of 32-bit words currently waiting in the gateware FIFO (WireOut 0x23).
    virtual uint32_t available() = 0;
    // Fill `buffer` completely (`size` is a multiple of kAudioPacketBytes) or throw.
    virtual void read(uint8_t* buffer, std::size_t size) = 0;
    // DDR4 ring occupancy, refreshed by available(). ringPeak() is 0 until the ring has held a packet (also 0 for a
    // gateware built without the ring).
    virtual uint32_t ringUsed() const { return 0; }
    virtual uint32_t ringPeak() const { return 0; }
};

// Where recorded files go.
class IAudioFileSink {
public:
    virtual ~IAudioFileSink() = default;
    virtual void write(const uint8_t* data, std::size_t n) = 0;
    virtual void overwrite(uint64_t offset, const uint8_t* data, std::size_t n) = 0;
    virtual void close() = 0;
};
using AudioSinkFactory = std::function<std::unique_ptr<IAudioFileSink>(const std::string& path)>;

class FileAudioSink : public IAudioFileSink {
public:
    explicit FileAudioSink(const std::string& path) {
        m_f.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        if (!m_f) throw std::runtime_error("cannot create " + path);
    }
    ~FileAudioSink() override { try { close(); } catch (...) {} }
    void write(const uint8_t* data, std::size_t n) override {
        m_f.write(reinterpret_cast<const char*>(data), std::streamsize(n));
        if (!m_f) throw std::runtime_error("disk write failed (disk full?)");
    }
    void overwrite(uint64_t offset, const uint8_t* data, std::size_t n) override {
        m_f.seekp(std::streamoff(offset));
        write(data, n);
    }
    void close() override { if (m_f.is_open()) m_f.close(); }

private:
    std::fstream m_f;
};

inline AudioSinkFactory defaultAudioSinkFactory() {
    return [](const std::string& path) -> std::unique_ptr<IAudioFileSink> { return std::make_unique<FileAudioSink>(path); };
}

// True if `dir` has room for a recording (a segment being written plus margin).
inline bool audioDiskSpaceOk(const std::string& dir, double segmentSeconds, std::string& why, double bytesPerSecond = 36.3e6) {
    std::error_code ec;
    const auto info = std::filesystem::space(dir, ec);
    const double need = 3.0 * segmentSeconds * bytesPerSecond;
    if (ec) { why = "cannot query the free space of " + dir + ": " + ec.message(); return false; }
    if (double(info.available) < need) {
        char b[160];
        std::snprintf(b, sizeof b, "not enough disk space in %s: %.1f GB free, %.1f GB needed", dir.c_str(),
                      double(info.available) / 1e9, need / 1e9);
        why = b;
        return false;
    }
    return true;
}

// Status bits of WireOut 0x20 and the counters of the microphone gateware (see AudioPipe.h for how they are read).
constexpr uint32_t kMicStatusRun     = 0x1;
constexpr uint32_t kMicStatusPresent = 0x4;
constexpr uint32_t kMicStatusFull    = 0x8;   // the DDR4 ring / FIFO was full at some point

struct MicCounters {
    uint32_t status = 0;
    uint32_t dropped = 0;    // packets the gateware dropped: its ring/FIFO was full
    uint32_t overruns = 0;
    uint32_t level = 0;      // words waiting in the audio FIFO
    uint32_t ringUsed = 0;   // packets in the DDR4 ring now
    uint32_t ringPeak = 0;   // maximum since the last reset: how deep the PC backlog got
};

struct AudioStats {
    uint64_t packets = 0;        // valid packets received
    uint64_t lostPackets = 0;    // inferred from seq gaps
    uint64_t badBlocks = 0;      // packets with an invalid header (stream resynchronised)
    uint64_t frames = 0;         // sample frames received (lost ones excluded)
    uint32_t lastSeq = 0;
    // Timebase of the most recent packet: the FPGA clock (ticks of kAudioTickHz) when its first frame was sampled, and the
    // latest video frame start the gateware saw (count + timestamp). Used to stamp recorded video frames.
    uint64_t lastTs = 0;
    uint32_t lastVideoFrames = 0;
    uint64_t lastVideoTs = 0;
    uint64_t recordedFrames = 0; // frames written to the current recording (silence included)
    uint64_t segments = 0;       // closed segments of the current recording
    double   maxGapMs = 0;       // longest time between two pump() calls (host stalls)
    uint64_t longGaps = 0;       // gaps longer than 3 ms
    double   readMs = 0;         // time spent waiting on / reading the pipe, cumulative
    double   decodeMs = 0;       // time spent decoding / recording, cumulative
    uint64_t skippedPackets = 0; // packets read but not decoded (catch-up mode)
    double   elapsedMs = 0;      // wall time since start()
};

struct AudioCaptureOptions {
    double      tickHz = kAudioTickHz;            // sampleRate = tickHz / kAudioTicksPerFrame
    std::size_t ringFrames = 65536;               // frames kept for recent() when keepRaw
    double      segmentSeconds = 60;              // a recording is cut into files of this duration
    std::size_t maxPacketsPerRead = 32;           // the gateware FIFO holds 32 packets
    bool        keepRaw = false;                  // keep the raw samples (recent()); the display needs only the peaks
    bool        catchUp = false;                  // behind and not recording: decode only the newest packet of each read
    // Called (from the writer thread) once a segment's WAV and CSV are closed.
    std::function<void(const std::string& wavPath, const std::string& csvPath)> onSegmentClosed;
};

class AudioCapture {
public:
    explicit AudioCapture(IAudioPipe& pipe, AudioCaptureOptions options = AudioCaptureOptions())
        : m_pipe(pipe), m_opt(std::move(options)) {
        m_sampleRate = uint32_t(m_opt.tickHz / kAudioTicksPerFrame + 0.5);
        m_segmentFrames = std::max<uint64_t>(1, uint64_t(m_opt.segmentSeconds * m_sampleRate + 0.5));
        m_peakBlocks = std::max<std::size_t>(1, m_opt.ringFrames / kAudioPeakBlock);
        m_peakMin.assign(m_peakBlocks * kAudioChannels, 0);
        m_peakMax.assign(m_peakBlocks * kAudioChannels, 0);
        if (m_opt.keepRaw) m_ring.assign(m_opt.ringFrames * kAudioChannels, 0);
        m_buffer.resize(std::size_t(kAudioPacketBytes) * std::max<std::size_t>(1, m_opt.maxPacketsPerRead));
    }
    ~AudioCapture() {
        stopRecording();
        stopWriter();
    }
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    uint32_t sampleRate() const { return m_sampleRate; }

    // Reset the counters (call when the microphones are (re)started: the gateware sequence number restarts too).
    void reset() {
        std::lock_guard<std::mutex> l(m_mx);
        m_stats = AudioStats();
        m_expectSeq = false;
        m_lastPumpValid = false;
        m_startTime = std::chrono::steady_clock::now();
        m_started = true;
        m_peakHead = m_peakFill = 0;
        m_accCount = 0;
        m_ringHead = m_ringFill = 0;
    }

    AudioStats stats() const {
        std::lock_guard<std::mutex> l(m_mx);
        AudioStats s = m_stats;
        if (m_started)
            s.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_startTime).count();
        return s;
    }

    // Reads what is currently waiting in the gateware FIFO (whole packets only) and processes it. Never waits for
    // data. Returns the number of packets processed. Call only from the device thread.
    std::size_t pump() {
        const auto t0 = Clock::now();
        {
            std::lock_guard<std::mutex> l(m_mx);
            if (m_lastPumpValid) {
                const double gap = ms(m_lastPump, t0);
                if (gap > m_stats.maxGapMs) m_stats.maxGapMs = gap;
                if (gap > 3) m_stats.longGaps++;
            }
            m_lastPump = t0;
            m_lastPumpValid = true;
        }
        const uint32_t words = m_pipe.available();
        std::size_t packets = words / (kAudioPacketBytes / 4);
        if (packets > m_opt.maxPacketsPerRead) packets = m_opt.maxPacketsPerRead;
        if (packets == 0) { addTimes(ms(t0, Clock::now()), 0); return 0; }
        m_pipe.read(m_buffer.data(), packets * kAudioPacketBytes);
        const auto t1 = Clock::now();
        // The PC is behind: with the DDR4 ring a backlog of several dozen ms is queued in it (the gateware FIFO in front of
        // it is always full then, which says nothing); without the ring a nearly full FIFO means the same. Without a
        // recording only the newest packet of each read is decoded, so the live display shows the present.
        const bool behindNow = m_pipe.ringPeak() > 0
            ? m_pipe.ringUsed() >= kAudioCatchUpRingPackets
            : words >= (m_opt.maxPacketsPerRead > 4 ? m_opt.maxPacketsPerRead - 4 : 0) * (kAudioPacketBytes / 4);
        const bool behind = m_opt.catchUp && !recording() && behindNow;
        for (std::size_t i = 0; i < packets; i++) {
            const uint8_t* p = &m_buffer[i * kAudioPacketBytes];
            if (behind && i + 1 < packets) skipPacket(p);
            else handle(p);
        }
        addTimes(ms(t0, t1), ms(t1, Clock::now()));
        return packets;
    }

    // Waits for ONE packet (a blocking read, ~0.11 ms at the nominal rate) and processes it. The microphones must stay
    // enabled while this runs.
    std::size_t pumpBlocking() {
        const auto t0 = Clock::now();
        m_pipe.read(m_buffer.data(), kAudioPacketBytes);
        const auto t1 = Clock::now();
        handle(m_buffer.data());
        addTimes(ms(t0, t1), ms(t1, Clock::now()));
        return 1;
    }

    // ---- recording ----------------------------------------------------------------------------------------------
    // Writes <dir>/<session>_<n>.wav and .csv through `factory` (default: files). Call from any thread.
    // `format`: lower sample rate and / or 16 bits to shrink the files (see AudioConvert.h); default: the 24-bit stream as is.
    void startRecording(const std::string& dir, const std::string& session, AudioSinkFactory factory = defaultAudioSinkFactory(),
                        AudioRecordFormat format = AudioRecordFormat()) {
        if (!format.valid()) throw std::invalid_argument("unsupported audio recording format");
        stopRecording();
        startWriter();
        RecPtr rec = std::make_shared<Recording>();
        rec->dir = dir;
        rec->session = session;
        rec->factory = std::move(factory);
        rec->format = format;
        if (!format.isCopy()) rec->converter = std::make_unique<AudioConverter>(format);
        {
            std::lock_guard<std::mutex> l(m_mx);
            m_stats.recordedFrames = 0;
            m_stats.segments = 0;
            m_recordError.clear();
        }
        std::lock_guard<std::mutex> l(m_recMx);
        m_rec = std::move(rec);
    }

    // Closes the current segment (waits for the pending writes) and stops recording. Call from any thread.
    void stopRecording() {
        RecPtr rec;
        {
            std::lock_guard<std::mutex> l(m_recMx);
            rec = std::move(m_rec);
            m_rec.reset();
            if (rec && rec->current) {
                flushChunk(rec, rec->current);
                postFinalize(rec, rec->current);
                rec->current.reset();
            }
        }
        if (rec) waitWriterIdle();
    }

    bool recording() const {
        std::lock_guard<std::mutex> l(m_recMx);
        return m_rec && !recFailed(*m_rec);
    }
    // Why the recording stopped by itself (disk full, write error, disk too slow...), empty otherwise.
    std::string recordError() const {
        std::lock_guard<std::mutex> l(m_mx);
        return m_recordError;
    }

    // ---- display ------------------------------------------------------------------------------------------------
    // Min/max of the last up-to-`maxBlocks` blocks (kAudioPeakBlock frames each) of one channel, oldest first.
    // Returns the number of blocks written to outMin/outMax (each at least maxBlocks long).
    std::size_t recentPeaks(int channel, std::size_t maxBlocks, int32_t* outMin, int32_t* outMax) const {
        std::lock_guard<std::mutex> l(m_mx);
        const std::size_t n = std::min(maxBlocks, m_peakFill);
        const std::size_t start = (m_peakHead + m_peakBlocks - n) % m_peakBlocks;
        for (std::size_t i = 0; i < n; i++) {
            const std::size_t o = ((start + i) % m_peakBlocks) * kAudioChannels + channel;
            outMin[i] = m_peakMin[o];
            outMax[i] = m_peakMax[o];
        }
        return n;
    }

    // Last up-to-`maxFrames` samples of one channel (sign-extended 24-bit), oldest first. Needs keepRaw.
    std::vector<int32_t> recent(int channel, std::size_t maxFrames) const {
        std::lock_guard<std::mutex> l(m_mx);
        if (!m_opt.keepRaw) return {};
        const std::size_t n = std::min(maxFrames, m_ringFill);
        std::vector<int32_t> out(n);
        const std::size_t start = (m_ringHead + m_opt.ringFrames - n) % m_opt.ringFrames;
        for (std::size_t i = 0; i < n; i++) out[i] = m_ring[((start + i) % m_opt.ringFrames) * kAudioChannels + channel];
        return out;
    }

private:
    using Clock = std::chrono::steady_clock;
    static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
    void addTimes(double readMs, double decodeMs) {
        std::lock_guard<std::mutex> l(m_mx);
        m_stats.readMs += readMs;
        m_stats.decodeMs += decodeMs;
    }

    // ---- recording internals ------------------------------------------------------------------------------------
    struct Segment {
        std::string wavPath, csvPath;
        std::unique_ptr<IAudioFileSink> sink;
        std::vector<uint8_t> chunk;       // being filled by the device thread
        std::size_t chunkLen = 0;
        uint64_t frames = 0;              // input frames (device thread): segment length, CSV
        uint64_t outFrames = 0;           // frames written to the file (writer thread only): WAV header
        std::vector<std::string> csv;
    };
    struct Recording {
        std::string dir, session;
        AudioSinkFactory factory;
        AudioRecordFormat format;
        std::unique_ptr<AudioConverter> converter;   // writer thread only; null = copy
        int segment = 0;
        std::shared_ptr<Segment> current;
        std::atomic<std::size_t> queuedBytes{0};
        std::string failed;               // the reason; written/read under m_mx only
        std::atomic<bool> hasFailed{false};   // lock-free mirror of !failed.empty() for the per-packet checks
    };
    using RecPtr = std::shared_ptr<Recording>;
    using SegPtr = std::shared_ptr<Segment>;

    void failRecording(Recording& rec, const std::string& why) {
        std::lock_guard<std::mutex> l(m_mx);
        if (!rec.failed.empty()) return;
        rec.failed = why;
        rec.hasFailed.store(true);
        m_recordError = why;
    }
    static bool recFailed(const Recording& rec) { return rec.hasFailed.load(); }

    // Lock order: m_recMx, then m_mx (never the other way round).
    void record(const AudioPacketInfo& h, uint64_t lostFrames) {
        std::lock_guard<std::mutex> rl(m_recMx);
        const RecPtr rec = m_rec;
        if (!rec || recFailed(*rec)) return;
        try {
            if (!rec->current) rec->current = openSegment(rec);
            const SegPtr seg = rec->current;
            if (lostFrames > 0) {
                // keep the file position proportional to time: silence in place of the lost packets
                static const std::vector<uint8_t> zeros(std::size_t(kAudioFrameBytes) * 1024, 0);
                uint64_t left = lostFrames;
                while (left > 0) {
                    const uint64_t n = std::min<uint64_t>(left, 1024);
                    append(rec, seg, zeros.data(), std::size_t(n) * kAudioFrameBytes);
                    left -= n;
                }
                seg->frames += lostFrames;
                addRecorded(lostFrames);
            }
            seg->csv.push_back(std::to_string(h.seq) + ',' + std::to_string(h.ts) + ',' + std::to_string(seg->frames / uint64_t(rec->format.decimation)) + ',' +
                               std::to_string(h.videoFrames) + ',' + std::to_string(h.videoTs) + ',' + (h.lostBefore ? "1" : "0"));
            append(rec, seg, h.payload, std::size_t(h.nsamp) * kAudioFrameBytes);
            seg->frames += h.nsamp;
            addRecorded(h.nsamp);
            if (!recFailed(*rec) && seg->frames >= m_segmentFrames) {
                // Close in the background: the acquisition must not wait for the disk.
                flushChunk(rec, seg);
                postFinalize(rec, seg);
                rec->current.reset();
                rec->segment++;
            }
        } catch (const std::exception& e) {
            failRecording(*rec, e.what());
        }
    }
    void addRecorded(uint64_t frames) {
        std::lock_guard<std::mutex> l(m_mx);
        m_stats.recordedFrames += frames;
    }

    SegPtr openSegment(const RecPtr& rec) {
        char name[64];
        std::snprintf(name, sizeof name, "_%03d", rec->segment);
        auto seg = std::make_shared<Segment>();
        const std::string base = (std::filesystem::path(rec->dir) / (rec->session + name)).string();
        seg->wavPath = base + ".wav";
        seg->csvPath = base + ".csv";
        seg->sink = rec->factory(seg->wavPath);
        seg->chunk.assign(kAudioRecChunkBytes, 0);
        seg->csv.push_back("seq,ts,first_frame,video_frames,video_ts,lost_before");
        // placeholder header, rewritten at the end; written straight by the writer thread (it must not go through the converter)
        std::vector<uint8_t> hdr = buildWavHeader(0, m_sampleRate / uint32_t(rec->format.decimation), rec->format.bytesPerSample());
        post([this, rec, seg, hdr = std::move(hdr)]() {
            try {
                if (!recFailed(*rec)) seg->sink->write(hdr.data(), hdr.size());
            } catch (const std::exception& e) {
                failRecording(*rec, e.what());
            }
        });
        return seg;
    }

    // Copies bytes into the current chunk; a full chunk is handed to the writer thread (never waited for here).
    void append(const RecPtr& rec, const SegPtr& seg, const uint8_t* data, std::size_t n) {
        std::size_t off = 0;
        while (off < n) {
            const std::size_t k = std::min(n - off, seg->chunk.size() - seg->chunkLen);
            std::memcpy(&seg->chunk[seg->chunkLen], data + off, k);
            seg->chunkLen += k;
            off += k;
            if (seg->chunkLen == seg->chunk.size()) flushChunk(rec, seg);
        }
    }

    void flushChunk(const RecPtr& rec, const SegPtr& seg) {
        if (seg->chunkLen == 0) return;
        std::vector<uint8_t> buf(seg->chunk.begin(), seg->chunk.begin() + std::ptrdiff_t(seg->chunkLen));
        seg->chunkLen = 0;
        const std::size_t queued = rec->queuedBytes.fetch_add(buf.size()) + buf.size();
        if (queued > kAudioRecMaxQueuedBytes) {
            rec->queuedBytes.fetch_sub(buf.size());
            failRecording(*rec, "disk write queue overflow: the disk cannot keep up with the audio");
            return;
        }
        post([this, rec, seg, buf = std::move(buf)]() {
            try {
                if (!recFailed(*rec)) {
                    if (!rec->converter) {
                        seg->sink->write(buf.data(), buf.size());
                        seg->outFrames += buf.size() / kAudioFrameBytes;
                    } else {
                        std::vector<uint8_t> out;
                        seg->outFrames += rec->converter->process(buf.data(), buf.size() / kAudioFrameBytes, out);
                        if (!out.empty()) seg->sink->write(out.data(), out.size());
                    }
                }
            } catch (const std::exception& e) {
                failRecording(*rec, e.what());
            }
            rec->queuedBytes.fetch_sub(buf.size());
        });
    }

    // Writes the end of a segment (header sizes, CSV) from the writer thread; never throws: errors stop the recording.
    void postFinalize(const RecPtr& rec, const SegPtr& seg) {
        post([this, rec, seg]() {
            try {
                if (recFailed(*rec)) { seg->sink->close(); return; }
                const std::vector<uint8_t> hdr = buildWavHeader(seg->outFrames * uint64_t(rec->format.bytesPerFrame()),
                                                                m_sampleRate / uint32_t(rec->format.decimation), rec->format.bytesPerSample());
                seg->sink->overwrite(0, hdr.data(), hdr.size());
                seg->sink->close();
                {
                    std::ofstream csv(seg->csvPath, std::ios::trunc);
                    for (const std::string& line : seg->csv) csv << line << '\n';
                    if (!csv) throw std::runtime_error("cannot write " + seg->csvPath);
                }
                { std::lock_guard<std::mutex> l(m_mx); m_stats.segments++; }
                if (m_opt.onSegmentClosed) m_opt.onSegmentClosed(seg->wavPath, seg->csvPath);
            } catch (const std::exception& e) {
                failRecording(*rec, e.what());
            }
        });
    }

    // ---- writer thread ------------------------------------------------------------------------------------------
    void startWriter() {
        std::lock_guard<std::mutex> l(m_wMx);
        if (m_writer.joinable()) return;
        m_wStop = false;
        m_writer = std::thread([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> l(m_wMx);
                    m_wCv.wait(l, [this] { return m_wStop || !m_wQueue.empty(); });
                    if (m_wQueue.empty()) return;   // stopping and nothing left
                    task = std::move(m_wQueue.front());
                    m_wQueue.pop_front();
                    m_wBusy = true;
                }
                task();
                {
                    std::lock_guard<std::mutex> l(m_wMx);
                    m_wBusy = false;
                }
                m_wIdleCv.notify_all();
            }
        });
    }
    void post(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> l(m_wMx);
            m_wQueue.push_back(std::move(task));
        }
        m_wCv.notify_one();
    }
    void waitWriterIdle() {
        std::unique_lock<std::mutex> l(m_wMx);
        m_wIdleCv.wait(l, [this] { return m_wQueue.empty() && !m_wBusy; });
    }
    void stopWriter() {
        {
            std::lock_guard<std::mutex> l(m_wMx);
            m_wStop = true;
        }
        m_wCv.notify_all();
        if (m_writer.joinable()) m_writer.join();
    }

    // ---- decoding -----------------------------------------------------------------------------------------------
    // Counts a packet (sequence / loss tracking) without decoding its samples (catch-up mode).
    void skipPacket(const uint8_t* p) {
        AudioPacketInfo h;
        std::lock_guard<std::mutex> l(m_mx);
        if (!parseAudioPacket(p, h)) { m_stats.badBlocks++; return; }
        trackPacketLocked(h);
        m_stats.skippedPackets++;
    }

    // Sequence / loss / timebase bookkeeping shared by skipPacket() and handle(). m_mx held. Returns the number of
    // sample frames lost before this packet (from the gap in `seq`, modulo 2^32).
    uint64_t trackPacketLocked(const AudioPacketInfo& h) {
        uint64_t lostFrames = 0;
        if (m_expectSeq && h.seq != m_nextSeq) {
            const uint32_t gap = h.seq - m_nextSeq;
            m_stats.lostPackets += gap;
            lostFrames = uint64_t(gap) * h.nsamp;
        }
        m_expectSeq = true;
        m_nextSeq = h.seq + 1;
        m_stats.packets++;
        m_stats.frames += h.nsamp;
        m_stats.lastSeq = h.seq;
        m_stats.lastTs = h.ts;
        m_stats.lastVideoFrames = h.videoFrames;
        m_stats.lastVideoTs = h.videoTs;
        return lostFrames;
    }

    void handle(const uint8_t* p) {
        AudioPacketInfo h;
        uint64_t lostFrames = 0;
        {
            std::lock_guard<std::mutex> l(m_mx);
            if (!parseAudioPacket(p, h)) { m_stats.badBlocks++; return; }
            lostFrames = trackPacketLocked(h);
            decodeLocked(h);
        }
        record(h, lostFrames);
    }

    // Decodes once: raw ring (optional) and per-block min/max peaks. m_mx held.
    void decodeLocked(const AudioPacketInfo& h) {
        const uint8_t* pay = h.payload;
        for (uint32_t i = 0; i < h.nsamp; i++) {
            const uint8_t* f = pay + std::size_t(i) * kAudioFrameBytes;
            int32_t v[kAudioChannels];                       // sign-extended 24-bit samples of this frame
            for (int c = 0; c < kAudioChannels; c++) {
                const uint8_t* s = f + c * 3;
                v[c] = (int32_t(uint32_t(s[0]) | uint32_t(s[1]) << 8 | uint32_t(s[2]) << 16) << 8) >> 8;
            }
            if (m_accCount == 0) {                           // first frame of a block: start the min/max from it
                std::memcpy(m_accMin, v, sizeof v);
                std::memcpy(m_accMax, v, sizeof v);
            } else {
                for (int c = 0; c < kAudioChannels; c++) {
                    m_accMin[c] = std::min(m_accMin[c], v[c]);
                    m_accMax[c] = std::max(m_accMax[c], v[c]);
                }
            }
            if (m_opt.keepRaw) std::memcpy(&m_ring[m_ringHead * kAudioChannels], v, sizeof v);
            if (m_opt.keepRaw) {
                m_ringHead = (m_ringHead + 1) % m_opt.ringFrames;
                if (m_ringFill < m_opt.ringFrames) m_ringFill++;
            }
            if (++m_accCount >= kAudioPeakBlock) {
                std::memcpy(&m_peakMin[m_peakHead * kAudioChannels], m_accMin, sizeof m_accMin);
                std::memcpy(&m_peakMax[m_peakHead * kAudioChannels], m_accMax, sizeof m_accMax);
                m_peakHead = (m_peakHead + 1) % m_peakBlocks;
                if (m_peakFill < m_peakBlocks) m_peakFill++;
                m_accCount = 0;
            }
        }
    }

    IAudioPipe& m_pipe;
    AudioCaptureOptions m_opt;
    uint32_t m_sampleRate = 0;
    uint64_t m_segmentFrames = 0;
    std::vector<uint8_t> m_buffer;

    mutable std::mutex m_mx;              // stats, peaks, raw ring, record error
    AudioStats m_stats;
    std::string m_recordError;
    bool m_expectSeq = false;
    uint32_t m_nextSeq = 0;
    Clock::time_point m_lastPump, m_startTime;
    bool m_lastPumpValid = false, m_started = false;
    std::vector<int32_t> m_ring;
    std::size_t m_ringHead = 0, m_ringFill = 0;
    std::size_t m_peakBlocks = 1;
    std::vector<int32_t> m_peakMin, m_peakMax;
    std::size_t m_peakHead = 0, m_peakFill = 0;
    int m_accCount = 0;
    int32_t m_accMin[kAudioChannels] = {0}, m_accMax[kAudioChannels] = {0};

    mutable std::mutex m_recMx;           // m_rec and the device thread's use of it
    RecPtr m_rec;

    std::mutex m_wMx;
    std::condition_variable m_wCv, m_wIdleCv;
    std::deque<std::function<void()>> m_wQueue;
    std::thread m_writer;
    bool m_wStop = false, m_wBusy = false;
};

}  // namespace okcli
