/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// VideoRecorder.h — records the captured video frames (packed RGB, 8 bit) to a file, alongside the audio recording.
//
// writeFrame() is called by the capture thread for every frame it reads: it only copies the frame into a queue and
// returns, so recording never slows the capture. A writer thread opens the output on the first frame (the frame size is then
// known), writes the frames and, when the queue is full (disk or encoder too slow), DROPS frames (counted) instead of waiting.
//
// Output, chosen automatically (codec "auto") or by name:
//   h264  : H.264 in an .mp4 through `ffmpeg` (libx264, ultrafast, CRF 20): ~10-20x smaller than raw, light on the CPU
//   mjpeg : Motion-JPEG .avi through ffmpeg (if libx264 is missing)
//   ffv1  : lossless .mkv through ffmpeg
//   raw   : headerless 8-bit RGB frames (.rgb24), no external program. ~190 MB/s at 1080p30: only for fast disks.
// ffmpeg is looked up as `ffmpeg` (PATH) or the OKCAM_FFMPEG environment variable / VideoRecorderOptions::ffmpegPath.
//
// Every recording also gets a CSV with one row per written frame (<session>_video.csv: frame, host_ms since the start,
// and the FPGA timebase the audio side knew when the PC read the frame: latest audio packet timestamp, video frame-start
// counter and timestamp) and a text file describing the format, so that the video can be aligned with the audio files
// (<session>_NNN.wav + .csv written by AudioCapture), which carry the same FPGA clock. The PC does not learn from the
// hardware which frame start a given frame came from: the counter tells the order of frame starts, the offset between a
// frame and its frame start (a constant pipeline latency of a few frames) is calibrated once with a visible + audible event.
//
// Header-only, no dependency on the FrontPanel API.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace okcli {

constexpr std::size_t kVideoDefaultMaxQueuedBytes = std::size_t(256) << 20;

// What the audio side knew when the PC read the frame (see the header comment).
struct VideoSync {
    bool valid = false;
    uint64_t audioTs = 0;       // FPGA clock (ticks of 99.96 MHz) of the latest audio packet
    uint32_t sofCount = 0;      // video frame starts the gateware had counted
    uint64_t sofTs = 0;         // FPGA clock of the latest video frame start
    uint32_t audioSeq = 0;
};

class IVideoSink {
public:
    virtual ~IVideoSink() = default;
    virtual bool write(const uint8_t* rgb, std::size_t bytes, std::string& err) = 0;
    virtual bool close(std::string& err) = 0;
    virtual std::string path() const = 0;
};

// Creates the sink of one file ("part"): `base` is the path without extension.
using VideoSinkFactory = std::function<std::unique_ptr<IVideoSink>(
    const std::string& base, int width, int height, double fps, const std::string& codec, const std::string& ffmpeg,
    std::string& err)>;

struct VideoRecorderOptions {
    std::string codec = "auto";                 // auto | h264 | mjpeg | ffv1 | raw
    std::string ffmpegPath;                     // default: $OKCAM_FFMPEG or "ffmpeg"
    double fps = 30;                            // nominal rate written in the container (real timing: the CSV)
    std::size_t maxQueuedBytes = kVideoDefaultMaxQueuedBytes;
    VideoSinkFactory sinkFactory;               // tests: custom sinks
};

struct VideoStats {
    bool armed = false;               // recording requested (the file opens with the first frame)
    uint64_t framesWritten = 0;
    uint64_t framesDropped = 0;       // queue full: the encoder / disk could not keep up
    int parts = 0;                    // files opened (a new one starts when the frame size changes)
    std::string encoder;              // "h264 (ffmpeg)", "raw", ...
    std::string path;                 // current output file
    std::string error;                // why the recording stopped by itself
};

namespace video_detail {

inline std::string quote(const std::string& s) { return "\"" + s + "\""; }

// A command line for popen: on Windows cmd.exe strips the first and last quote of a line that contains several
inline std::string forShell(const std::string& cmd) {
#ifdef _WIN32
    return "\"" + cmd + "\"";
#else
    return cmd;
#endif
}

inline FILE* openPipe(const std::string& cmd, const char* mode) {
#ifdef _WIN32
    return _popen(forShell(cmd).c_str(), mode);
#else
    return popen(forShell(cmd).c_str(), mode);
#endif
}
inline int closePipe(FILE* f) {
#ifdef _WIN32
    return _pclose(f);
#else
    return pclose(f);
#endif
}

// ffmpeg executable: "ffmpeg" (PATH) unless a usual Windows install location has one
inline std::string findFfmpeg() {
#ifdef _WIN32
    std::vector<std::string> cands;
    for (const char* v : {"LOCALAPPDATA", "ProgramFiles", "ProgramData", "USERPROFILE"})
        if (const char* e = std::getenv(v)) {
            const std::string b = e;
            if (std::string(v) == "LOCALAPPDATA") cands.push_back(b + "\\Microsoft\\WinGet\\Links\\ffmpeg.exe");
            if (std::string(v) == "ProgramFiles") cands.push_back(b + "\\ffmpeg\\bin\\ffmpeg.exe");
            if (std::string(v) == "ProgramData") cands.push_back(b + "\\chocolatey\\bin\\ffmpeg.exe");
            if (std::string(v) == "USERPROFILE") cands.push_back(b + "\\scoop\\shims\\ffmpeg.exe");
        }
    cands.push_back("C:\\ffmpeg\\bin\\ffmpeg.exe");
    for (const auto& c : cands) { std::error_code ec; if (std::filesystem::exists(c, ec)) return c; }
#endif
    return "ffmpeg";
}

// "h264" / "mjpeg" / "ffv1" encoders ffmpeg offers ("" if ffmpeg cannot be run)
inline std::string ffmpegEncoders(const std::string& ffmpeg) {
    FILE* p = openPipe(quote(ffmpeg) + " -hide_banner -encoders 2>&1", "r");
    if (!p) return std::string();
    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    closePipe(p);
    return out;
}

inline std::string resolveCodec(const std::string& requested, const std::string& encoders) {
    const bool has264 = encoders.find("libx264") != std::string::npos;
    const bool hasMjpeg = encoders.find("mjpeg") != std::string::npos;
    const bool hasFfv1 = encoders.find("ffv1") != std::string::npos;
    if (requested == "raw") return "raw";
    if (requested == "h264") return has264 ? "h264" : "raw";
    if (requested == "mjpeg") return hasMjpeg ? "mjpeg" : "raw";
    if (requested == "ffv1") return hasFfv1 ? "ffv1" : "raw";
    return has264 ? "h264" : (hasMjpeg ? "mjpeg" : "raw");    // auto
}

inline const char* extensionFor(const std::string& codec) {
    if (codec == "h264") return ".mp4";
    if (codec == "mjpeg") return ".avi";
    if (codec == "ffv1") return ".mkv";
    return ".rgb24";
}

class RawSink : public IVideoSink {
public:
    explicit RawSink(const std::string& path) : m_path(path), m_f(path, std::ios::binary | std::ios::trunc) {
        if (!m_f) throw std::runtime_error("cannot create " + path);
    }
    bool write(const uint8_t* rgb, std::size_t bytes, std::string& err) override {
        m_f.write(reinterpret_cast<const char*>(rgb), std::streamsize(bytes));
        if (!m_f) { err = "disk write failed (disk full?)"; return false; }
        return true;
    }
    bool close(std::string& err) override {
        m_f.close();
        if (m_f.fail()) { err = "closing " + m_path + " failed"; return false; }
        return true;
    }
    std::string path() const override { return m_path; }

private:
    std::string m_path;
    std::ofstream m_f;
};

class FfmpegSink : public IVideoSink {
public:
    FfmpegSink(const std::string& path, const std::string& logPath, int w, int h, double fps, const std::string& codec,
               const std::string& ffmpeg)
        : m_path(path) {
        std::string enc;
        if (codec == "h264") enc = "-c:v libx264 -preset ultrafast -crf 20 -pix_fmt yuv420p -vf \"scale=trunc(iw/2)*2:trunc(ih/2)*2\"";
        else if (codec == "mjpeg") enc = "-c:v mjpeg -q:v 2";
        else enc = "-c:v ffv1 -level 3";
        char head[256];
        std::snprintf(head, sizeof head, " -hide_banner -loglevel warning -y -f rawvideo -pix_fmt rgb24 -video_size %dx%d -framerate %g -i - ",
                      w, h, fps);
        const std::string cmd = quote(ffmpeg) + head + enc + " " + quote(path) + " 2> " + quote(logPath);
#ifdef _WIN32
        m_pipe = openPipe(cmd, "wb");      // binary: the frames must not be altered by newline translation
#else
        m_pipe = openPipe(cmd, "w");       // glibc popen() accepts only "r" / "w"
#endif
        if (!m_pipe) throw std::runtime_error("cannot start ffmpeg (" + ffmpeg + ")");
    }
    ~FfmpegSink() override { std::string e; close(e); }
    bool write(const uint8_t* rgb, std::size_t bytes, std::string& err) override {
        if (!m_pipe) { err = "ffmpeg is not running"; return false; }
        if (std::fwrite(rgb, 1, bytes, m_pipe) != bytes) { err = "ffmpeg stopped accepting frames (see the _ffmpeg.log file)"; return false; }
        return true;
    }
    bool close(std::string& err) override {
        if (!m_pipe) return true;
        std::fflush(m_pipe);
        const int rc = closePipe(m_pipe);      // waits for ffmpeg to finish the file
        m_pipe = nullptr;
        if (rc != 0) { err = "ffmpeg exited with an error (see the _ffmpeg.log file)"; return false; }
        return true;
    }
    std::string path() const override { return m_path; }

private:
    std::string m_path;
    FILE* m_pipe = nullptr;
};

inline std::unique_ptr<IVideoSink> defaultSink(const std::string& base, int w, int h, double fps, const std::string& codec,
                                               const std::string& ffmpeg, std::string& err) {
    try {
        const std::string path = base + extensionFor(codec);
        if (codec == "raw") return std::make_unique<RawSink>(path);
        return std::make_unique<FfmpegSink>(path, base + "_ffmpeg.log", w, h, fps, codec, ffmpeg);
    } catch (const std::exception& e) {
        err = e.what();
        return nullptr;
    }
}

}  // namespace video_detail

// True if `dir` has room for a video recording of the given codec (a few minutes at least).
inline bool videoDiskSpaceOk(const std::string& dir, const std::string& codec, std::string& why) {
    std::error_code ec;
    const auto info = std::filesystem::space(dir, ec);
    if (ec) { why = "cannot query the free space of " + dir + ": " + ec.message(); return false; }
    const double need = (codec == "raw") ? 20e9 : 2e9;
    if (double(info.available) < need) {
        char b[200];
        std::snprintf(b, sizeof b, "not enough disk space in %s: %.1f GB free, %.0f GB wanted for %s video", dir.c_str(),
                      double(info.available) / 1e9, need / 1e9, codec == "raw" ? "raw" : "encoded");
        why = b;
        return false;
    }
    return true;
}

class VideoRecorder {
public:
    VideoRecorder() = default;
    ~VideoRecorder() { stop(); }
    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    // Arms the recording. Nothing is opened until the first frame arrives (its size is then known). Call from any thread.
    // Returns false (+ why) if the output folder is unusable.
    bool start(const std::string& dir, const std::string& session, VideoRecorderOptions options, std::string& why) {
        stop();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!std::filesystem::is_directory(dir)) { why = "cannot use the folder " + dir; return false; }
        if (options.ffmpegPath.empty()) {
            const char* env = std::getenv("OKCAM_FFMPEG");
            options.ffmpegPath = (env && *env) ? env : video_detail::findFfmpeg();
        }
        const bool customSink = static_cast<bool>(options.sinkFactory);
        std::string encoders;
        if (!customSink) {
            options.sinkFactory = video_detail::defaultSink;
            if (options.codec != "raw") {
                encoders = video_detail::ffmpegEncoders(options.ffmpegPath);
                // No silent fallback to raw RGB (~190 MB/s at 1080p30): if the user did not ask for "raw", refuse instead.
                if (video_detail::resolveCodec(options.codec, encoders) == "raw") {
                    why = encoders.find("Encoders:") == std::string::npos      // the shell's "not found" message, not ffmpeg's list
                        ? "ffmpeg not found (looked for \"" + options.ffmpegPath + "\"). Install it, add it to the PATH, or locate it "
                          "(set OKCAM_FFMPEG); OKCAM_VIDEO_CODEC=raw records uncompressed video."
                        : "this ffmpeg has no usable '" + options.codec + "' encoder (libx264/mjpeg/ffv1); OKCAM_VIDEO_CODEC=raw records uncompressed video.";
                    return false;
                }
            }
        }
        {
            std::lock_guard<std::mutex> l(m_mx);
            m_opt = std::move(options);
            m_dir = dir;
            m_session = session;
            m_codec = customSink ? m_opt.codec : video_detail::resolveCodec(m_opt.codec, encoders);
            m_stats = VideoStats();
            m_stats.armed = true;
            m_stats.encoder = m_codec == "raw" ? "raw RGB" : (m_codec + " (ffmpeg)");
            m_queue.clear();
            m_queuedBytes = 0;
            m_startTime = Clock::now();
            m_armed = true;
            m_stop = false;
        }
        m_thread = std::thread([this] { run(); });
        return true;
    }

    bool recording() const { std::lock_guard<std::mutex> l(m_mx); return m_armed; }
    std::string codec() const { std::lock_guard<std::mutex> l(m_mx); return m_codec; }
    VideoStats stats() const {
        std::lock_guard<std::mutex> l(m_mx);
        VideoStats s = m_stats;
        s.armed = m_armed;
        return s;
    }

    // Copies the frame (packed RGB, w*h*3 bytes) into the queue; never waits. False if dropped (queue full) or not recording.
    bool writeFrame(const uint8_t* rgb, int w, int h, const VideoSync& sync) {
        const std::size_t bytes = std::size_t(w) * std::size_t(h) * 3;
        std::unique_lock<std::mutex> l(m_mx);
        if (!m_armed) return false;
        if (m_queuedBytes + bytes > m_opt.maxQueuedBytes) { m_stats.framesDropped++; return false; }
        Item it;
        if (!m_pool.empty()) { it.data = std::move(m_pool.back()); m_pool.pop_back(); }
        it.data.assign(rgb, rgb + bytes);
        it.w = w; it.h = h; it.sync = sync;
        it.hostMs = std::chrono::duration<double, std::milli>(Clock::now() - m_startTime).count();
        m_queuedBytes += bytes;
        m_queue.push_back(std::move(it));
        l.unlock();
        m_cv.notify_one();
        return true;
    }

    // Stops recording and waits until everything queued is written and the file(s) closed. Call from any thread.
    void stop() {
        {
            std::lock_guard<std::mutex> l(m_mx);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard<std::mutex> l(m_mx);
        m_armed = false;
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Item {
        std::vector<uint8_t> data;
        int w = 0, h = 0;
        VideoSync sync;
        double hostMs = 0;
    };

    void fail(const std::string& why) {
        std::lock_guard<std::mutex> l(m_mx);
        if (m_stats.error.empty()) m_stats.error = why;
        m_armed = false;            // new frames are refused; the queue is discarded by the writer
    }

    void run() {
        std::unique_ptr<IVideoSink> sink;
        int curW = 0, curH = 0, part = -1;
        std::ofstream csv;
        uint64_t frameNo = 0;
        auto closeSink = [&]() {
            if (!sink) return;
            std::string err;
            if (!sink->close(err) && !err.empty()) fail(err);
            sink.reset();
        };
        for (;;) {
            Item it;
            {
                std::unique_lock<std::mutex> l(m_mx);
                m_cv.wait(l, [this] { return m_stop || !m_queue.empty(); });
                if (m_queue.empty()) break;                       // stopping and nothing left
                it = std::move(m_queue.front());
                m_queue.pop_front();
            }
            const std::size_t bytes = it.data.size();
            bool failed;
            {
                std::lock_guard<std::mutex> l(m_mx);
                failed = !m_stats.error.empty();
            }
            if (!failed) {
                if (!sink || it.w != curW || it.h != curH) {      // first frame, or the frame size changed: a new file
                    closeSink();
                    part++;
                    curW = it.w; curH = it.h;
                    char suffix[16];
                    std::snprintf(suffix, sizeof suffix, part == 0 ? "" : "_p%02d", part);
                    const std::string base = (std::filesystem::path(m_dir) / (m_session + "_video" + suffix)).string();
                    std::string err;
                    sink = m_opt.sinkFactory(base, curW, curH, m_opt.fps, m_codec, m_opt.ffmpegPath, err);
                    if (!sink) {
                        fail(err.empty() ? "cannot open the video output" : err);
                    } else {
                        {
                            std::lock_guard<std::mutex> l(m_mx);
                            m_stats.parts = part + 1;
                            m_stats.path = sink->path();
                        }
                        writeInfo(base, curW, curH);
                        if (!csv.is_open()) {
                            csv.open((std::filesystem::path(m_dir) / (m_session + "_video.csv")).string(), std::ios::trunc);
                            csv << "frame,host_ms,audio_ts,sof_count,sof_ts,audio_seq,part\n";
                        }
                    }
                }
                if (sink) {
                    std::string err;
                    if (sink->write(it.data.data(), bytes, err)) {
                        csv << frameNo << ',' << it.hostMs << ',';
                        if (it.sync.valid) csv << it.sync.audioTs << ',' << it.sync.sofCount << ',' << it.sync.sofTs << ',' << it.sync.audioSeq;
                        else csv << ",,,";                                // no audio timebase: empty columns
                        csv << ',' << part << '\n';
                        frameNo++;
                        std::lock_guard<std::mutex> l(m_mx);
                        m_stats.framesWritten++;
                    } else {
                        fail(err.empty() ? "video write failed" : err);
                    }
                }
            }
            {
                std::lock_guard<std::mutex> l(m_mx);
                m_queuedBytes -= bytes;
                if (m_pool.size() < 8) m_pool.push_back(std::move(it.data));
            }
        }
        closeSink();
        if (csv.is_open()) csv.close();
    }

    void writeInfo(const std::string& base, int w, int h) {
        std::ofstream f(base + "_info.txt", std::ios::trunc);
        f << "okCameraApp video recording\n"
          << "file: " << m_stats.path << "\n"
          << "size: " << w << "x" << h << "\n"
          << "pixel_format: rgb24 (8-bit R,G,B per pixel) as read from the camera pipeline\n"
          << "encoder: " << m_stats.encoder << "\n"
          << "nominal_fps: " << m_opt.fps << " (real timing: <session>_video.csv)\n"
          << "fpga_tick_hz: 99960000 (audio_ts / sof_ts columns of the CSV, and the ts columns of the audio CSVs)\n"
          << "sof_count: number of video frame starts the gateware had counted when the PC read the frame; the offset between\n"
          << "  a frame and its frame start is a constant pipeline latency (a few frames): calibrate it once with an event that\n"
          << "  is both visible and audible.\n";
    }

    mutable std::mutex m_mx;
    std::condition_variable m_cv;
    std::deque<Item> m_queue;
    std::vector<std::vector<uint8_t>> m_pool;       // recycled frame buffers (no allocation per frame)
    std::size_t m_queuedBytes = 0;
    VideoRecorderOptions m_opt;
    std::string m_dir, m_session, m_codec;
    VideoStats m_stats;
    Clock::time_point m_startTime;
    bool m_armed = false, m_stop = false;
    std::thread m_thread;
};

}  // namespace okcli
