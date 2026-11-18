// Test hors materiel de VideoRecorder.h : H.264 par ffmpeg (relu par ffprobe), flux brut, CSV d'horodatages, changement de
// resolution, abandon d'images quand l'encodeur est lent (sans jamais bloquer l'appelant), panne d'ecriture, repli sans ffmpeg.
// Build : g++ -std=c++17 -I../src video_recorder_test.cpp -pthread -o video_recorder_test
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "VideoRecorder.h"

using namespace okcli;
namespace fs = std::filesystem;

static std::vector<uint8_t> makeFrame(int w, int h, int n) {
    std::vector<uint8_t> f(std::size_t(w) * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = &f[(std::size_t(y) * w + x) * 3];
            p[0] = uint8_t(x + n * 3); p[1] = uint8_t(y * 2 + n); p[2] = uint8_t(n * 5);
        }
    return f;
}
static fs::path tmpDir(const char* tag) {
    fs::path d = fs::temp_directory_path() / (std::string("okvideo_") + tag + "_" + std::to_string(std::rand()));
    fs::create_directories(d);
    return d;
}
static std::string readAll(const fs::path& p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static int countLines(const fs::path& p) { std::ifstream f(p); std::string l; int n = 0; while (std::getline(f, l)) n++; return n; }

int main() {
    const bool haveFfmpeg = std::system("ffmpeg -version > /dev/null 2>&1") == 0 && std::system("ffprobe -version > /dev/null 2>&1") == 0;

    // ---- raw output: exact bytes + CSV + info file ---------------------------------------------------------------------------
    {
        const fs::path dir = tmpDir("raw");
        VideoRecorder rec;
        VideoRecorderOptions opt; opt.codec = "raw";
        std::string why;
        assert(rec.start(dir.string(), "okcam_t", opt, why));
        assert(rec.recording());
        const int W = 64, H = 48, N = 30;
        for (int n = 0; n < N; n++) {
            VideoSync s; s.valid = true; s.audioTs = 1000000 + n * 3332000ull; s.sofCount = 100 + n; s.sofTs = s.audioTs - 500; s.audioSeq = 7 + n;
            assert(rec.writeFrame(makeFrame(W, H, n).data(), W, H, s));
        }
        rec.stop();
        const VideoStats st = rec.stats();
        assert(st.framesWritten == N && st.framesDropped == 0 && st.error.empty() && st.parts == 1);
        const fs::path rawf = dir / "okcam_t_video.rgb24";
        assert(fs::file_size(rawf) == uint64_t(N) * W * H * 3);
        std::ifstream f(rawf, std::ios::binary);
        std::vector<uint8_t> all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        assert(std::equal(all.begin() + std::ptrdiff_t(5 * W * H * 3), all.begin() + std::ptrdiff_t(6 * W * H * 3), makeFrame(W, H, 5).begin()));
        assert(countLines(dir / "okcam_t_video.csv") == N + 1);
        const std::string csv = readAll(dir / "okcam_t_video.csv");
        assert(csv.find("frame,host_ms,audio_ts,sof_count,sof_ts,audio_seq,part") == 0);
        assert(csv.find(",103,") != std::string::npos);          // sof_count of frame 3
        assert(readAll(dir / "okcam_t_video_info.txt").find("size: 64x48") != std::string::npos);
        fs::remove_all(dir);
    }

    // ---- H.264 through ffmpeg, read back by ffprobe --------------------------------------------------------------------------------
    if (haveFfmpeg) {
        const fs::path dir = tmpDir("h264");
        VideoRecorder rec;
        VideoRecorderOptions opt;      // codec "auto" -> h264 when libx264 is there
        std::string why;
        assert(rec.start(dir.string(), "okcam_h", opt, why));
        const int W = 320, H = 240, N = 60;
        for (int n = 0; n < N; n++) {
            // the queue (256 MiB) holds everything here; retry only if frames were dropped, and give up if the recorder stopped
            for (int tries = 0; !rec.writeFrame(makeFrame(W, H, n).data(), W, H, VideoSync()); tries++) {
                assert(rec.recording() && tries < 400);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        rec.stop();
        const VideoStats st = rec.stats();
        std::cout << "encodeur: " << st.encoder << " ecrit=" << st.framesWritten << " perdu=" << st.framesDropped << " erreur='" << st.error << "'\n";
        assert(st.error.empty() && st.framesWritten == N);
        const std::string cmd = "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames "
                                "-of default=nw=1 \"" + (dir / "okcam_h_video.mp4").string() + "\" > \"" + (dir / "probe.txt").string() + "\"";
        assert(std::system(cmd.c_str()) == 0);
        const std::string probe = readAll(dir / "probe.txt");
        std::cout << probe;
        assert(probe.find("codec_name=h264") != std::string::npos && probe.find("width=320") != std::string::npos &&
               probe.find("height=240") != std::string::npos && probe.find("nb_read_frames=60") != std::string::npos);
        fs::remove_all(dir);
    }

    // ---- the frame size changes: a new part ------------------------------------------------------------------------------------
    {
        const fs::path dir = tmpDir("parts");
        VideoRecorder rec;
        VideoRecorderOptions opt; opt.codec = "raw";
        std::string why;
        assert(rec.start(dir.string(), "okcam_p", opt, why));
        for (int n = 0; n < 5; n++) assert(rec.writeFrame(makeFrame(32, 24, n).data(), 32, 24, VideoSync()));
        for (int n = 0; n < 4; n++) assert(rec.writeFrame(makeFrame(48, 36, n).data(), 48, 36, VideoSync()));
        rec.stop();
        assert(rec.stats().parts == 2 && rec.stats().framesWritten == 9);
        assert(fs::file_size(dir / "okcam_p_video.rgb24") == 5u * 32 * 24 * 3);
        assert(fs::file_size(dir / "okcam_p_video_p01.rgb24") == 4u * 48 * 36 * 3);
        fs::remove_all(dir);
    }

    // ---- a slow encoder / disk: frames are dropped, the caller never waits -------------------------------------------------------------
    {
        struct SlowSink : IVideoSink {
            std::atomic<int>* n;
            explicit SlowSink(std::atomic<int>* c) : n(c) {}
            bool write(const uint8_t*, std::size_t, std::string&) override { std::this_thread::sleep_for(std::chrono::milliseconds(30)); ++*n; return true; }
            bool close(std::string&) override { return true; }
            std::string path() const override { return "slow"; }
        };
        std::atomic<int> written{0};
        const fs::path dir = tmpDir("slow");
        VideoRecorder rec;
        VideoRecorderOptions opt;
        opt.codec = "test";
        opt.maxQueuedBytes = 5u * 64 * 48 * 3;    // room for 5 frames
        opt.sinkFactory = [&](const std::string&, int, int, double, const std::string&, const VideoEncodeSettings&, std::string&) {
            return std::unique_ptr<IVideoSink>(new SlowSink(&written));
        };
        std::string why;
        assert(rec.start(dir.string(), "okcam_s", opt, why));
        const auto f = makeFrame(64, 48, 0);
        const auto t0 = std::chrono::steady_clock::now();
        for (int n = 0; n < 100; n++) rec.writeFrame(f.data(), 64, 48, VideoSync());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        assert(ms < 100);                           // 100 calls while the sink needs 30 ms per frame: never blocked
        rec.stop();
        const VideoStats st = rec.stats();
        std::cout << "lent: appels en " << ms << " ms, ecrites=" << st.framesWritten << " perdues=" << st.framesDropped << "\n";
        assert(st.framesDropped > 50 && st.framesWritten >= 5 && st.framesWritten + st.framesDropped == 100);
        fs::remove_all(dir);
    }

    // ---- a failing sink stops the recording, never the caller ---------------------------------------------------------------------------
    {
        struct FailSink : IVideoSink {
            int n = 0;
            bool write(const uint8_t*, std::size_t, std::string& e) override { if (++n > 3) { e = "disk write failed (disk full?)"; return false; } return true; }
            bool close(std::string&) override { return true; }
            std::string path() const override { return "fail"; }
        };
        const fs::path dir = tmpDir("fail");
        VideoRecorder rec;
        VideoRecorderOptions opt; opt.codec = "test";
        opt.sinkFactory = [](const std::string&, int, int, double, const std::string&, const VideoEncodeSettings&, std::string&) { return std::unique_ptr<IVideoSink>(new FailSink()); };
        std::string why;
        assert(rec.start(dir.string(), "okcam_f", opt, why));
        const auto f = makeFrame(16, 16, 0);
        for (int n = 0; n < 50; n++) { rec.writeFrame(f.data(), 16, 16, VideoSync()); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        rec.stop();
        assert(rec.stats().error.find("disk") != std::string::npos && rec.stats().framesWritten == 3);
        assert(!rec.recording());
        fs::remove_all(dir);
    }

    // ---- ffmpeg missing: refused (no silent 190 MB/s raw file); raw only when explicitly requested ---------------------------------------------------------------------------------------------------
    {
        const fs::path dir = tmpDir("nofm");
        VideoRecorder rec;
        VideoRecorderOptions opt; opt.ffmpegPath = "/nonexistent/ffmpeg";
        std::string why;
        assert(!rec.start(dir.string(), "okcam_n", opt, why) && why.find("ffmpeg not found") == 0 && !rec.recording());
        opt.codec = "raw";
        assert(rec.start(dir.string(), "okcam_n", opt, why));
        assert(rec.codec() == "raw");
        assert(rec.writeFrame(makeFrame(16, 16, 0).data(), 16, 16, VideoSync()));
        rec.stop();
        assert(fs::exists(dir / "okcam_n_video.rgb24") && rec.stats().error.empty());
        fs::remove_all(dir);
    }

    // ---- frame divider: 1 frame out of 3, nominal rate divided, skipped frames are not "dropped" ------------------------------------------------
    {
        struct CountSink : IVideoSink {
            std::atomic<int>* n;
            bool write(const uint8_t*, std::size_t, std::string&) override { (*n)++; return true; }
            bool close(std::string&) override { return true; }
            std::string path() const override { return "count"; }
        };
        std::atomic<int> n{0}; std::atomic<double> fpsSeen{0};
        const fs::path dir = tmpDir("div");
        VideoRecorder rec;
        VideoRecorderOptions opt; opt.codec = "test"; opt.fps = 30; opt.frameDivider = 3;
        opt.sinkFactory = [&](const std::string&, int, int, double fps, const std::string&, const VideoEncodeSettings&, std::string&) {
            fpsSeen = fps; auto s = std::make_unique<CountSink>(); s->n = &n; return std::unique_ptr<IVideoSink>(std::move(s));
        };
        std::string why;
        assert(rec.start(dir.string(), "okcam_div", opt, why));
        const auto f = makeFrame(16, 16, 0);
        for (int i = 0; i < 30; i++) assert(rec.writeFrame(f.data(), 16, 16, VideoSync()));
        rec.stop();
        assert(n == 10 && rec.stats().framesWritten == 10 && rec.stats().framesDropped == 0 && fpsSeen == 10.0);
        fs::remove_all(dir);
    }

    // ---- real ffmpeg (skipped when absent): quality / picture size really change the file ----------------------------------------------------------
    if (!video_detail::ffmpegEncoders("ffmpeg").empty() && video_detail::hasEncoderList(video_detail::ffmpegEncoders("ffmpeg")) &&
        video_detail::ffmpegEncoders("ffmpeg").find("libx264") != std::string::npos) {
        auto run = [&](int crf, int scale, int divider, const char* tag, uintmax_t& size, std::string& dims) {
            const fs::path dir = tmpDir(tag);
            VideoRecorder rec;
            VideoRecorderOptions opt; opt.codec = "h264"; opt.crf = crf; opt.scalePercent = scale; opt.frameDivider = divider;
            std::string why;
            assert(rec.start(dir.string(), "okcam_q", opt, why));
            for (int n = 0; n < 24; n++) {                         // 320 x 180 moving pattern with noise
                std::vector<uint8_t> f(320 * 180 * 3);
                for (int y = 0; y < 180; y++) for (int x = 0; x < 320; x++) for (int c = 0; c < 3; c++)
                    f[(y * 320 + x) * 3 + c] = uint8_t(((x + 3 * n) * (c + 1) + y * 5) ^ ((x * 7919 + y * 104729 + n * 13 + c) >> 3 & 0x3F));
                rec.writeFrame(f.data(), 320, 180, VideoSync());
            }
            rec.stop();
            assert(rec.stats().error.empty());
            size = fs::file_size(dir / "okcam_q_video.mp4");
            FILE* p = popen(("ffprobe -v error -select_streams v:0 -show_entries stream=width,height,nb_frames -of csv=p=0 \"" + (dir / "okcam_q_video.mp4").string() + "\" 2>&1").c_str(), "r");
            char b[128] = {0}; if (p) { (void)!fgets(b, sizeof b, p); pclose(p); }
            dims = b;
            fs::remove_all(dir);
        };
        uintmax_t big, small, half, thirds; std::string d1, d2, d3, d4;
        run(18, 100, 1, "q1", big, d1); run(35, 100, 1, "q2", small, d2); run(23, 50, 1, "q3", half, d3); run(23, 100, 3, "q4", thirds, d4);
        std::cout << "h264 crf18 " << big << " B (" << d1 << ") crf35 " << small << " B  50% " << half << " B (" << d3 << ")  1/3 frames " << thirds << " B (" << d4 << ")\n";
        assert(small < big / 2);                                   // quality 35 is much smaller than 18
        assert(d1.rfind("320,180", 0) == 0 && d3.rfind("160,90", 0) == 0);   // 50 % picture: half the width and height
        assert(d4.find(",8") != std::string::npos);                // 24 frames / 3 = 8 recorded
    }

    // ---- slow ffmpeg probe: start() does not wait for it (the application used to freeze for seconds on Windows) -------------------------
#ifndef _WIN32
    {
        const fs::path dir = tmpDir("slowprobe");
        auto script = [&](const char* name, const char* encoders) {
            const fs::path f = dir / name;
            std::ofstream(f) << "#!/bin/sh\ncase \"$*\" in *-encoders*) sleep 1; echo Encoders:; echo \" V..... " << encoders << "\";;\n*) cat > /dev/null;; esac\n";
            fs::permissions(f, fs::perms::owner_all);
            return f.string();
        };
        for (const bool good : {true, false}) {
            VideoRecorder rec;
            VideoRecorderOptions opt;
            opt.ffmpegPath = script(good ? "ffgood" : "ffbad", good ? "libx264" : "nothing");
            std::string why;
            const auto t0 = std::chrono::steady_clock::now();
            assert(rec.start((dir / "out").string(), "slow", opt, why));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            assert(ms < 600);                                  // the probe takes 1 s: start() returned long before
            for (int i = 0; i < 3; i++) rec.writeFrame(makeFrame(16, 16, i).data(), 16, 16, VideoSync());
            rec.stop();
            if (good) assert(rec.stats().error.empty() && rec.stats().framesWritten == 3 && rec.codec() == "h264");
            else assert(rec.stats().error.find("no usable") != std::string::npos);
        }
        fs::remove_all(dir);
    }
#endif

    // ---- unusable folder / disk-space helper ---------------------------------------------------------------------------------------------------
    {
        VideoRecorder rec;
        std::string why;
        assert(!rec.start("/proc/forbidden/dir", "x", VideoRecorderOptions(), why) && !why.empty());
        assert(videoDiskSpaceOk(fs::temp_directory_path().string(), "h264", why) || !why.empty());
    }

    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
