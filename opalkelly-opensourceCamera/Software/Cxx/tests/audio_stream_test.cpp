// Test hors materiel de AudioStream.h (equivalent du test TypeScript audioStream.test.ts) : flux synthetique avec
// paquets perdus, pipe simulee, segments, ecriture par blocs, panne de disque, rattrapage, fichier WAV RF64.
// Build : g++ -std=c++17 -I../src audio_stream_test.cpp -pthread -o audio_stream_test
#include <cassert>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "AudioStream.h"

using namespace okcli;
namespace fs = std::filesystem;

static constexpr int NS = 21;

static int32_t sampleValue(uint64_t g, int c) { return int32_t((g * 977 + c * 131) % 16000000) - 8000000; }

static void put32(std::vector<uint8_t>& v, std::size_t wordIdx, uint32_t x) {
    for (int i = 0; i < 4; i++) v[4 * wordIdx + i] = uint8_t(x >> (8 * i));
}

static std::vector<uint8_t> makePacket(uint32_t seq, uint32_t vframes, bool lostBefore) {
    std::vector<uint8_t> p(kAudioPacketBytes, 0);
    const uint64_t ts = 1000000ull + uint64_t(seq) * NS * 528;
    put32(p, 0, kAudioMagic); put32(p, 1, seq); put32(p, 2, uint32_t(ts)); put32(p, 3, uint32_t(ts >> 32));
    put32(p, 4, NS | (lostBefore ? 1u << 16 : 0)); put32(p, 5, vframes); put32(p, 6, 0); put32(p, 7, 0);
    for (int i = 0; i < NS; i++)
        for (int c = 0; c < 64; c++) {
            const uint32_t v = uint32_t(sampleValue(uint64_t(seq) * NS + i, c)) & 0xFFFFFF;
            uint8_t* d = &p[32 + i * 192 + c * 3];
            d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); d[2] = uint8_t(v >> 16);
        }
    return p;
}

class FakePipe : public IAudioPipe {
public:
    std::deque<std::vector<uint8_t>> queue;
    uint32_t ringUsedV = 0, ringPeakV = 0;
    void push(const std::vector<uint8_t>& p) { queue.push_back(p); }
    uint32_t available() override { return uint32_t(queue.size() * kAudioPacketBytes / 4); }
    void read(uint8_t* buf, std::size_t n) override {
        for (std::size_t i = 0; i < n / kAudioPacketBytes; i++) {
            std::memcpy(buf + i * kAudioPacketBytes, queue.front().data(), kAudioPacketBytes);
            queue.pop_front();
        }
    }
    uint32_t ringUsed() const override { return ringUsedV; }
    uint32_t ringPeak() const override { return ringPeakV; }
};

static fs::path tmpDir(const char* tag) {
    fs::path d = fs::temp_directory_path() / (std::string("okaudio_") + tag + "_" + std::to_string(std::rand()));
    fs::create_directories(d);
    return d;
}
static void drain(AudioCapture& c) { while (c.pump() > 0) {} }

int main() {
    // ---- parse sanity ---------------------------------------------------------------------------------------------
    {
        const auto p = makePacket(7, 3, true);
        AudioPacketInfo h;
        assert(parseAudioPacket(p.data(), h));
        assert(h.seq == 7 && h.nsamp == NS && h.lostBefore && h.videoFrames == 3 && h.ts == 1000000ull + 7ull * NS * 528);
        std::vector<uint8_t> zero(kAudioPacketBytes, 0);
        assert(!parseAudioPacket(zero.data(), h));
    }

    // ---- segmented recording with lost packets ----------------------------------------------------------------------
    {
        FakePipe pipe;
        const int TOTAL = 400;
        auto isLost = [](int s) { return s == 10 || s == 11 || s == 50 || s == 51 || s == 52; };
        int sent = 0;
        for (int s = 0; s < TOTAL; s++) if (!isLost(s)) { pipe.push(makePacket(uint32_t(s), uint32_t(sent++ / 10), isLost(s - 1))); }
        const fs::path dir = tmpDir("seg");
        std::vector<std::string> closed;
        std::mutex cm;
        AudioCaptureOptions opt;
        opt.keepRaw = true; opt.ringFrames = 5000; opt.segmentSeconds = 0.02;   // ~3786 frames = 181 packets per segment
        opt.onSegmentClosed = [&](const std::string& wav, const std::string&) { std::lock_guard<std::mutex> l(cm); closed.push_back(wav); };
        AudioCapture cap(pipe, opt);
        assert(cap.sampleRate() == 189318);
        cap.reset();
        cap.startRecording(dir.string(), "okcam_test");
        drain(cap);
        cap.stopRecording();
        const AudioStats st = cap.stats();
        std::cout << "packets=" << st.packets << " perdus=" << st.lostPackets << " recorded=" << st.recordedFrames
                  << " segments=" << st.segments << "\n";
        assert(st.packets == uint64_t(TOTAL - 5) && st.lostPackets == 5 && st.badBlocks == 0);
        assert(st.recordedFrames == uint64_t(TOTAL) * NS);   // lost packets replaced by silence: full duration
        assert(st.segments >= 2 && closed.size() == st.segments);
        // the timebase of the latest packet is kept to stamp recorded video frames (VideoSync)
        assert(st.lastTs == 1000000ull + uint64_t(TOTAL - 1) * NS * 528 && st.lastVideoFrames == 39 && st.lastSeq == uint32_t(TOTAL - 1));

        // files frame by frame across the segments
        std::vector<fs::path> wavs;
        for (auto& e : fs::directory_iterator(dir)) if (e.path().extension() == ".wav") wavs.push_back(e.path());
        std::sort(wavs.begin(), wavs.end());
        assert(wavs.size() == st.segments);
        uint64_t g = 0;
        for (const auto& w : wavs) {
            std::ifstream f(w, std::ios::binary);
            std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            assert(std::memcmp(&buf[0], "RF64", 4) == 0);
            const uint64_t frames = (buf.size() - 104) / kAudioFrameBytes;
            const uint64_t ds = uint64_t(audioWord(&buf[0], 7)) | uint64_t(audioWord(&buf[0], 8)) << 32;
            assert(ds == frames * kAudioFrameBytes);
            assert(audioWord(&buf[0], 15) == 189318);
            for (uint64_t i = 0; i < frames; i++, g++) {
                const int seq = int(g / NS);
                for (int c = 0; c < 64; c += 9) {
                    const uint8_t* d = &buf[104 + i * kAudioFrameBytes + c * 3];
                    int32_t v = int32_t(d[0] | d[1] << 8 | d[2] << 16); v = (v << 8) >> 8;
                    const int32_t exp = isLost(seq) ? 0 : sampleValue(g, c);
                    if (v != exp) { std::cerr << "mismatch " << w << " frame " << i << " ch " << c << "\n"; return 1; }
                }
            }
            std::ifstream cf(fs::path(w).replace_extension(".csv"));
            std::string line; std::getline(cf, line);
            assert(line == "seq,ts,first_frame,video_frames,video_ts,lost_before");
            assert(std::getline(cf, line));
        }
        assert(g == uint64_t(TOTAL) * NS);

        // display: raw ring and peaks agree
        const auto rec = cap.recent(5, 100);
        assert(rec.size() == 100 && rec.back() == sampleValue(uint64_t(TOTAL) * NS - 1, 5));
        const std::size_t B = 40;
        std::vector<int32_t> pmin(B), pmax(B);
        for (int ch : {0, 5, 63}) {
            assert(cap.recentPeaks(ch, B, pmin.data(), pmax.data()) == B);
            const std::size_t tail = st.frames % kAudioPeakBlock;
            const auto raw = cap.recent(ch, B * kAudioPeakBlock + tail);
            for (std::size_t b = 0; b < B; b++) {
                const auto first = raw.begin() + std::ptrdiff_t(b * kAudioPeakBlock);
                assert(pmin[b] == *std::min_element(first, first + kAudioPeakBlock));
                assert(pmax[b] == *std::max_element(first, first + kAudioPeakBlock));
            }
        }
        // independent reader
        if (std::system("ffprobe -version > /dev/null 2>&1") == 0) {
            const std::string cmd = "ffprobe -v error -show_entries stream=codec_name,channels,sample_rate,bits_per_sample -of default=nw=1 \"" +
                                    wavs[0].string() + "\" > " + (dir / "probe.txt").string();
            assert(std::system(cmd.c_str()) == 0);
            std::ifstream pf(dir / "probe.txt");
            std::stringstream ss; ss << pf.rdbuf();
            std::cout << ss.str();
            assert(ss.str().find("pcm_s24le") != std::string::npos && ss.str().find("channels=64") != std::string::npos);
        }
        fs::remove_all(dir);
    }

    // ---- recording is batched (one write per MiB) ---------------------------------------------------------------------
    {
        struct CountingSink : FileAudioSink {
            std::atomic<int>* writes;
            CountingSink(const std::string& p, std::atomic<int>* w) : FileAudioSink(p), writes(w) {}
            void write(const uint8_t* d, std::size_t n) override { ++*writes; FileAudioSink::write(d, n); }
        };
        std::atomic<int> writes{0};
        FakePipe pipe;
        const int NPK = 600;
        for (int q = 0; q < NPK; q++) pipe.push(makePacket(uint32_t(q), 0, false));
        const fs::path dir = tmpDir("batch");
        AudioCaptureOptions opt; opt.segmentSeconds = 600;
        AudioCapture cap(pipe, opt);
        cap.startRecording(dir.string(), "okcam_batch", [&](const std::string& p) { return std::unique_ptr<IAudioFileSink>(new CountingSink(p, &writes)); });
        drain(cap);
        cap.stopRecording();
        assert(writes < NPK / 20);
        assert(fs::file_size(dir / "okcam_batch_000.wav") == 104 + uint64_t(NPK) * NS * kAudioFrameBytes);
        fs::remove_all(dir);
    }

    // ---- a failing disk stops the recording, never the acquisition -------------------------------------------------------
    {
        struct FullDiskSink : FileAudioSink {
            int n = 0;
            using FileAudioSink::FileAudioSink;
            void write(const uint8_t* d, std::size_t k) override {
                if (++n > 2) throw std::runtime_error("disk write failed (disk full?)");
                FileAudioSink::write(d, k);
            }
        };
        FakePipe pipe;
        for (int q = 0; q < 800; q++) pipe.push(makePacket(uint32_t(q), 0, false));
        const fs::path dir = tmpDir("fail");
        AudioCaptureOptions opt; opt.segmentSeconds = 600;
        AudioCapture cap(pipe, opt);
        cap.startRecording(dir.string(), "okcam_fail", [](const std::string& p) { return std::unique_ptr<IAudioFileSink>(new FullDiskSink(p)); });
        drain(cap);
        assert(cap.stats().packets == 800);                      // the acquisition continued
        for (int i = 0; i < 200 && cap.recordError().empty(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(cap.recordError().find("disk") != std::string::npos);
        assert(!cap.recording());
        cap.stopRecording();                                     // must not throw
        fs::remove_all(dir);
    }

    // ---- catch-up: FIFO nearly full (no ring) / ring backlog -----------------------------------------------------------
    {
        FakePipe pipe;
        for (int q = 0; q < 40; q++) if (q != 3 && q != 4 && q != 20) pipe.push(makePacket(uint32_t(q), 0, false));
        AudioCaptureOptions opt; opt.catchUp = true;
        AudioCapture cap(pipe, opt);
        drain(cap);
        const AudioStats st = cap.stats();
        assert(st.packets == 37 && st.lostPackets == 3);
        assert(st.skippedPackets > 0 && st.skippedPackets < st.packets);

        auto run = [&](uint32_t used) {
            FakePipe rp; rp.ringPeakV = 1; rp.ringUsedV = used;
            for (int q = 0; q < 40; q++) rp.push(makePacket(uint32_t(q), 0, false));
            AudioCapture c(rp, opt);
            drain(c);
            return c.stats().skippedPackets;
        };
        assert(run(5) == 0);          // no ring backlog: nothing skipped even with a full FIFO
        assert(run(500) > 0);         // large ring backlog: packets skipped
    }

    // ---- pumpBlocking: exactly one packet --------------------------------------------------------------------------------
    {
        FakePipe pipe;
        pipe.push(makePacket(0, 0, false));
        AudioCapture cap(pipe);
        assert(cap.pumpBlocking() == 1 && cap.stats().packets == 1);
    }

    // ---- disk space helper ------------------------------------------------------------------------------------------------
    {
        std::string why;
        assert(audioDiskSpaceOk(fs::temp_directory_path().string(), 0.001, why));
        assert(!audioDiskSpaceOk("/nonexistent/dir/for/test", 1, why) && !why.empty());
    }

    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
