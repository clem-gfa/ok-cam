/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AudioStream.h — capture of the 64-channel PDM microphone stream (XEM8310 / DARWIN).
//
// The gateware (Gateware/XEM8310-AU25P/Microphones) sends 4096-byte packets on block pipe 0xA1:
//   8 header words (magic "MIC1", seq, 64-bit timestamp of the first sample, nsamp + flags, video
//   frame count, 64-bit timestamp of the last video frame start), then nsamp (21) sample frames of
//   64 channels x 24-bit signed little-endian, then zero padding. A sample frame is exactly the
//   interleaved layout of a 64-channel 24-bit WAV, so the payload is written to the file as is.
//
// Lost packets (gap in `seq`) are replaced by silence in the WAV so that file position stays
// proportional to time, and are counted. A sidecar CSV records, per packet, the timestamps needed to
// align the audio with the video (frame start timestamps and frame counts).
//
// Header-only, no dependency on the FrontPanel API: the byte source is the IAudioPipe interface.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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
constexpr int      kAudioTicksPerFrame = 480;      // 20 clk per PDM period x 24 (decimation)

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

// Source of bytes (the FrontPanel block pipe, or a test double). Must return exactly `size` bytes
// or throw. `size` is always a multiple of kAudioPacketBytes.
class IAudioPipe {
public:
    virtual ~IAudioPipe() = default;
    virtual void read(uint8_t* buffer, std::size_t size) = 0;
};

// Multi-channel 24-bit PCM in an RF64 file (valid beyond 4 GiB; sizes finalised on close).
class WavWriter {
public:
    WavWriter() = default;
    ~WavWriter() { close(); }
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    void open(const std::string& path, uint32_t sampleRate) {
        close();
        m_f.open(path, std::ios::binary | std::ios::trunc);
        if (!m_f) throw std::runtime_error("cannot open " + path);
        m_rate = sampleRate; m_bytes = 0;
        writeHeader();
    }
    void write(const uint8_t* data, std::size_t n) {
        m_f.write(reinterpret_cast<const char*>(data), std::streamsize(n));
        m_bytes += n;
    }
    void writeSilence(std::size_t n) {
        static const uint8_t z[4096] = {0};
        while (n) { const std::size_t k = n < sizeof z ? n : sizeof z; write(z, k); n -= k; }
    }
    void setSampleRate(uint32_t r) { m_rate = r; }
    uint64_t dataBytes() const { return m_bytes; }
    bool isOpen() const { return m_f.is_open(); }
    void close() {
        if (!m_f.is_open()) return;
        if (m_bytes & 1) { const char z = 0; m_f.write(&z, 1); }   // chunk padding (frames are 192 bytes: never odd)
        writeHeader();
        m_f.close();
    }

private:
    static void u16(std::ostream& o, uint16_t v) { o.put(char(v)); o.put(char(v >> 8)); }
    static void u32(std::ostream& o, uint32_t v) { for (int i = 0; i < 4; i++) o.put(char(v >> (8*i))); }
    static void u64(std::ostream& o, uint64_t v) { for (int i = 0; i < 8; i++) o.put(char(v >> (8*i))); }
    void writeHeader() {
        m_f.seekp(0);
        const uint64_t riffSize = 4 + (8 + 28) /*ds64*/ + (8 + 40) /*fmt */ + (8 + m_bytes);
        m_f.write("RF64", 4); u32(m_f, 0xFFFFFFFFu); m_f.write("WAVE", 4);
        m_f.write("ds64", 4); u32(m_f, 28);
        u64(m_f, riffSize); u64(m_f, m_bytes); u64(m_f, m_bytes / kAudioFrameBytes); u32(m_f, 0);
        m_f.write("fmt ", 4); u32(m_f, 40);
        u16(m_f, 0xFFFE); u16(m_f, kAudioChannels); u32(m_f, m_rate); u32(m_f, m_rate * kAudioFrameBytes);
        u16(m_f, kAudioFrameBytes); u16(m_f, 24);
        u16(m_f, 22); u16(m_f, 24); u32(m_f, 0);            // extension size, valid bits, channel mask
        static const uint8_t pcm[16] = {1,0,0,0, 0,0, 0x10,0, 0x80,0, 0,0xAA,0, 0x38,0x9B,0x71};
        m_f.write(reinterpret_cast<const char*>(pcm), 16);   // KSDATAFORMAT_SUBTYPE_PCM
        m_f.write("data", 4); u32(m_f, 0xFFFFFFFFu);
        m_f.seekp(0, std::ios::end);
    }
    std::ofstream m_f;
    uint32_t m_rate = 200000;
    uint64_t m_bytes = 0;
};

struct AudioStats {
    uint64_t packets = 0;        // valid packets received
    uint64_t lostPackets = 0;    // inferred from seq gaps
    uint64_t badBlocks = 0;      // packets with an invalid header (stream resynchronised)
    uint64_t frames = 0;         // sample frames written (silence included)
    uint32_t lastSeq = 0;
    uint64_t lastTs = 0;
};

// Reads the pipe in a thread, decodes the packets, writes the WAV + CSV sidecar and keeps the most
// recent frames for display.
class AudioCapture {
public:
    // tickHz : frequency of the audio clock (timestamp ticks/s); sampleRate = tickHz / 480.
    AudioCapture(IAudioPipe& pipe, double tickHz = 95.5e6, std::size_t displayFrames = 100000)
        : m_pipe(pipe), m_tickHz(tickHz), m_ringCap(displayFrames), m_ring(displayFrames * kAudioFrameBytes) {}
    ~AudioCapture() { stop(); }

    // path empty -> no file, display only. Writes <path> (WAV) and <path>.csv.
    void start(const std::string& path = std::string()) {
        stop();
        m_stop = false; m_error.clear(); m_stats = AudioStats(); m_ringFill = 0; m_ringHead = 0;
        m_expectSeq = false;
        if (!path.empty()) {
            m_wav.open(path, uint32_t(m_tickHz / kAudioTicksPerFrame + 0.5));
            m_csv.open(path + ".csv", std::ios::trunc);
            m_csv << "seq,ts,first_frame,video_frames,video_ts,lost_before\n";
        }
        m_thread = std::thread([this] { run(); });
    }
    void stop() {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
        m_wav.close();
        if (m_csv.is_open()) m_csv.close();
    }
    AudioStats stats() const { std::lock_guard<std::mutex> l(m_mx); return m_stats; }
    std::string error() const { std::lock_guard<std::mutex> l(m_mx); return m_error; }
    bool running() const { return m_thread.joinable() && !m_done; }

    // Last up-to-`maxFrames` samples of one channel (sign-extended 24-bit), oldest first.
    std::vector<int32_t> recent(int channel, std::size_t maxFrames) const {
        std::lock_guard<std::mutex> l(m_mx);
        const std::size_t n = maxFrames < m_ringFill ? maxFrames : m_ringFill;
        std::vector<int32_t> out(n);
        for (std::size_t i = 0; i < n; i++) {
            const std::size_t idx = (m_ringHead + m_ringCap - n + i) % m_ringCap;
            const uint8_t* s = &m_ring[idx * kAudioFrameBytes + channel * 3];
            int32_t v = int32_t(s[0] | s[1] << 8 | s[2] << 16);
            out[i] = (v << 8) >> 8;
        }
        return out;
    }

private:
    void run() {
        m_done = false;
        std::vector<uint8_t> buf(std::size_t(kAudioPacketBytes) * 16);
        try {
            while (!m_stop) {
                m_pipe.read(buf.data(), buf.size());
                for (std::size_t off = 0; off + kAudioPacketBytes <= buf.size(); off += kAudioPacketBytes)
                    handle(&buf[off]);
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> l(m_mx);
            m_error = e.what();
        }
        m_done = true;
    }

    void handle(const uint8_t* p) {
        AudioPacketInfo h;
        std::lock_guard<std::mutex> l(m_mx);
        if (!parseAudioPacket(p, h)) { m_stats.badBlocks++; return; }
        const uint64_t firstFrame = m_stats.frames;
        if (m_expectSeq && h.seq != m_nextSeq) {
            const uint32_t gap = h.seq - m_nextSeq;   // modulo 2^32
            m_stats.lostPackets += gap;
            const std::size_t lostFrames = std::size_t(gap) * h.nsamp;
            if (m_wav.isOpen()) m_wav.writeSilence(lostFrames * kAudioFrameBytes);
            m_stats.frames += lostFrames;
        }
        m_expectSeq = true; m_nextSeq = h.seq + 1;
        if (m_wav.isOpen()) {
            m_wav.write(h.payload, std::size_t(h.nsamp) * kAudioFrameBytes);
            m_csv << h.seq << ',' << h.ts << ',' << (m_stats.frames) << ',' << h.videoFrames << ',' << h.videoTs
                  << ',' << (h.lostBefore ? 1 : 0) << '\n';
        }
        for (uint32_t i = 0; i < h.nsamp; i++) {
            std::memcpy(&m_ring[m_ringHead * kAudioFrameBytes], h.payload + std::size_t(i) * kAudioFrameBytes, kAudioFrameBytes);
            m_ringHead = (m_ringHead + 1) % m_ringCap;
            if (m_ringFill < m_ringCap) m_ringFill++;
        }
        (void)firstFrame;
        m_stats.frames += h.nsamp; m_stats.packets++; m_stats.lastSeq = h.seq; m_stats.lastTs = h.ts;
    }

    IAudioPipe& m_pipe;
    double m_tickHz;
    std::size_t m_ringCap;
    std::vector<uint8_t> m_ring;
    std::size_t m_ringHead = 0, m_ringFill = 0;
    mutable std::mutex m_mx;
    std::thread m_thread;
    std::atomic<bool> m_stop{false}, m_done{true};
    std::string m_error;
    AudioStats m_stats;
    bool m_expectSeq = false;
    uint32_t m_nextSeq = 0;
    WavWriter m_wav;
    std::ofstream m_csv;
};

}  // namespace okcli
