/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AcousticCamera.h — acoustic camera for the DARWIN board: delay-and-sum beamforming of the 64 microphones, giving a map of
// where the sound comes from, laid over the video image (the camera is in the 6 mm hole at the centre of the microphone spiral).
//
// Method (frequency-domain delay-and-sum, plane waves = far field):
//   * the last ~1000 frames of the 64 channels (a continuous signal, AudioCapture::recentFrames) are cut into 512-sample
//     segments (Hann window, 50 % overlap) and transformed (FFT);
//   * for every direction d of the image grid, the bins of the chosen band are steered: S(d,f) = sum_m X_m(f) exp(-j 2 pi f p_m.d / c)
//     (p_m: position of microphone m relative to the camera, c: speed of sound) and the power |S|^2 is summed over the band
//     and the segments;
//   * the map is that power in dB relative to its maximum. A point source shows as a blob centred on its direction.
//
// What can be expected from 64 microphones over an ~76 mm aperture: the beam width is about lambda / D = 13 degrees at 20 kHz
// (4 degrees at 60 kHz, 50 degrees at 5 kHz): use the highest band the source emits. The microphones are 8 to 10 mm apart, so
// spatial aliasing (ghost beams) starts around 20 kHz on a regular grid; the spiral layout spreads these ghosts, at a lower level.
//
// Image frame (camera frame): x to the right, y down, z forward (out of the board, towards the scene). The positions come from
// DARWIN.kicad_pcb (see Gateware/XEM8310-AU25P/Microphones/mic_map.md): the camera is at (100, 100) mm, and the board seen from
// the acoustic side and turned 90 degrees to the right matches the camera horizontal, i.e. in the camera frame
//     x = y_kicad - 100,   y = 100 - x_kicad          (a proper rotation of the top view; the mirror is only the viewpoint)
// If a source on the right of the image shows on the left (or up / down), tick "flip X" / "flip Y".
//
// Header-only, no dependency on the GUI or on the FrontPanel API.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AudioStream.h"

namespace okcli {

namespace acoustic_detail {
constexpr double kPi = 3.14159265358979323846;
// Centre of each SPH0641LU4H-1 in KiCad board coordinates (mm), index = channel - 1 = MK number - 1
constexpr float kKicadXY[64][2] = {
    {112.05f, 62.85f}, {98.67f, 63.62f}, {78.40f, 68.64f}, {105.66f, 68.89f},
    {87.35f, 68.93f}, {117.52f, 70.11f}, {95.21f, 72.12f}, {110.03f, 76.03f},
    {128.84f, 76.17f}, {79.57f, 77.47f}, {101.01f, 78.23f}, {120.09f, 78.29f},
    {87.82f, 78.75f}, {71.18f, 79.53f}, {95.04f, 84.18f}, {63.47f, 84.50f},
    {110.76f, 84.70f}, {129.17f, 85.03f}, {119.25f, 86.61f}, {137.70f, 86.65f},
    {84.71f, 87.11f}, {76.17f, 87.47f}, {103.18f, 88.17f}, {68.75f, 91.44f},
    {126.48f, 93.02f}, {94.73f, 93.13f}, {113.87f, 94.28f}, {86.72f, 95.15f},
    {135.43f, 95.58f}, {103.69f, 96.62f}, {77.29f, 96.94f}, {62.97f, 98.11f},
    {120.60f, 99.15f}, {109.85f, 101.74f}, {70.94f, 102.41f}, {130.69f, 102.84f},
    {82.39f, 103.87f}, {90.57f, 106.00f}, {99.38f, 107.04f}, {123.82f, 107.60f},
    {137.43f, 108.61f}, {114.99f, 108.68f}, {67.24f, 110.10f}, {76.86f, 110.69f},
    {106.10f, 111.74f}, {93.30f, 114.32f}, {130.64f, 114.52f}, {84.96f, 114.96f},
    {114.33f, 117.17f}, {122.71f, 117.59f}, {75.13f, 118.88f}, {66.30f, 119.09f},
    {102.49f, 119.20f}, {94.74f, 123.39f}, {84.94f, 123.42f}, {108.95f, 124.90f},
    {125.79f, 126.17f}, {117.05f, 127.10f}, {76.63f, 127.46f}, {99.85f, 130.00f},
    {88.47f, 131.10f}, {107.35f, 134.22f}, {116.34f, 135.82f}, {94.78f, 137.39f},
};
constexpr double kCameraX = 100.0, kCameraY = 100.0;     // the 6 mm hole
}  // namespace acoustic_detail

struct MicPosition {
    float x = 0, y = 0;      // mm relative to the camera, camera frame (x right, y down)
};

// Positions of the 64 microphones in the camera frame; flipX / flipY mirror them when the board is mounted differently from
// the assumption above.
inline std::array<MicPosition, kAudioChannels> darwinMicGeometry(bool flipX = false, bool flipY = false) {
    std::array<MicPosition, kAudioChannels> g;
    for (int m = 0; m < kAudioChannels; m++) {
        const float kx = acoustic_detail::kKicadXY[m][0], ky = acoustic_detail::kKicadXY[m][1];
        float x = ky - float(acoustic_detail::kCameraY);
        float y = float(acoustic_detail::kCameraX) - kx;
        if (flipX) x = -x;
        if (flipY) y = -y;
        g[std::size_t(m)] = {x, y};
    }
    return g;
}

struct AcousticSettings {
    double fLo = 8000, fHi = 20000;      // band analysed (Hz)
    double hfovDeg = 62.2;               // horizontal field of view of the camera (IMX219: 62.2 degrees)
    double aspect = 16.0 / 9.0;          // image width / height: gives the vertical field of view
    int gridW = 48, gridH = 27;          // directions computed (the display interpolates)
    double speedOfSound = 343.0;         // m/s
    bool flipX = false, flipY = false;
};

struct AcousticMap {
    int w = 0, h = 0;                    // grid
    std::vector<float> db;               // w * h, dB relative to the maximum (<= 0), floor -60
    int peakX = 0, peakY = 0;            // grid cell of the maximum
    float azimuthDeg = 0, elevationDeg = 0;   // direction of the maximum: azimuth + to the right, elevation + up
    float contrastDb = 0;                // maximum over the median of the map: how much a source stands out (0 = diffuse sound)
    float levelDb = -200;                // mean power per microphone in the band, dB re full scale (24 bit) squared
    std::vector<float> power;            // w * h, linear power (arbitrary but constant unit: what the baseline is made of)
    float tanX = 0, tanY = 0;            // tangents of the half fields of view the grid spans
    bool baselineApplied = false;        // the map shows what exceeds the baseline (see AcousticBaseline)
    bool empty = false;                  // nothing above the baseline (or no signal): the map is blank
    float snrDb = 0;                     // with a baseline: the live maximum over the baseline at the same place, dB
    double computeMs = 0;
    uint64_t sequence = 0;
    uint64_t endFrame = 0;               // audio frames received when this map was computed
};

// ---- FFT (radix-2, in place) ----------------------------------------------------------------------------------------------
namespace acoustic_detail {
inline void fft(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {          // bit reversal
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const float wr = float(std::cos(ang)), wi = float(std::sin(ang));
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                const int a = i + k, b = i + k + len / 2;
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                const float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

// From map.power: dB relative to the maximum, cell and direction of the maximum, contrast. Blank when the power is all zero.
inline void finishMap(AcousticMap& m) {
    const std::size_t n = m.power.size();
    m.db.assign(n, -60.0f);
    m.empty = false;
    std::size_t best = 0;
    for (std::size_t i = 1; i < n; i++) if (m.power[i] > m.power[best]) best = i;
    const float pmax = n ? m.power[best] : 0.0f;
    if (n == 0 || !(pmax > 0.0f)) {
        m.empty = true;
        m.peakX = m.w / 2; m.peakY = m.h / 2;
        m.azimuthDeg = m.elevationDeg = 0; m.contrastDb = 0;
        return;
    }
    for (std::size_t i = 0; i < n; i++) m.db[i] = std::max(-60.0f, 10.0f * std::log10(std::max(m.power[i], 1e-30f) / pmax));
    m.peakX = int(best % std::size_t(m.w));
    m.peakY = int(best / std::size_t(m.w));
    std::vector<float> sorted(m.db);
    std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(sorted.size() / 2), sorted.end());
    m.contrastDb = -sorted[sorted.size() / 2];
    const double pdx = ((m.peakX + 0.5) / m.w * 2 - 1) * m.tanX, pdy = ((m.peakY + 0.5) / m.h * 2 - 1) * m.tanY;
    m.azimuthDeg = float(std::atan(pdx) * 180.0 / kPi);
    m.elevationDeg = float(-std::atan(pdy / std::sqrt(pdx * pdx + 1.0)) * 180.0 / kPi);
}
}  // namespace acoustic_detail

class Beamformer {
public:
    static constexpr int kFft = 512;                // 2.7 ms at 189 kS/s: bins of 370 Hz
    static constexpr int kHop = kFft / 2;
    static constexpr int kMaxSegments = 3;
    static constexpr std::size_t kFramesNeeded = kFft + (kMaxSegments - 1) * kHop;   // 1024

    explicit Beamformer(double sampleRate) : m_fs(sampleRate) {
        m_window.resize(kFft);
        for (int i = 0; i < kFft; i++) m_window[std::size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * acoustic_detail::kPi * i / kFft));
    }

    // `frames`: n >= kFft interleaved frames (64 channels, 24-bit values). Returns false if there is not enough signal.
    bool compute(const int32_t* frames, std::size_t n, const AcousticSettings& s, AcousticMap& out) const {
        using clock = std::chrono::steady_clock;
        const auto t0 = clock::now();
        if (n < std::size_t(kFft) || s.gridW < 2 || s.gridH < 2) return false;
        const int M = kAudioChannels;
        const int nseg = int(std::min<std::size_t>(kMaxSegments, 1 + (n - kFft) / kHop));
        const std::size_t first = n - std::size_t(kFft) - std::size_t(nseg - 1) * kHop;   // newest data
        const double df = m_fs / kFft;
        const int kLo = std::max(1, int(std::ceil(s.fLo / df)));
        const int kHi = std::min(kFft / 2 - 1, int(std::floor(s.fHi / df)));
        if (kHi < kLo) return false;
        const int nb = kHi - kLo + 1;

        // spectra, structure of arrays: [bin][segment][mic]
        std::vector<float> xr(std::size_t(nb) * nseg * M), xi(xr.size());
        std::vector<float> re(kFft), im(kFft);
        double levelSum = 0;
        for (int sg = 0; sg < nseg; sg++)
            for (int m = 0; m < M; m++) {
                const int32_t* p = frames + (first + std::size_t(sg) * kHop) * M + m;
                for (int i = 0; i < kFft; i++) { re[std::size_t(i)] = float(p[std::size_t(i) * M]) * m_window[std::size_t(i)]; im[std::size_t(i)] = 0; }
                acoustic_detail::fft(re.data(), im.data(), kFft);
                for (int k = 0; k < nb; k++) {
                    const std::size_t o = (std::size_t(k) * nseg + std::size_t(sg)) * M + std::size_t(m);
                    xr[o] = re[std::size_t(kLo + k)]; xi[o] = im[std::size_t(kLo + k)];
                    levelSum += double(xr[o]) * xr[o] + double(xi[o]) * xi[o];
                }
            }

        const auto geo = darwinMicGeometry(s.flipX, s.flipY);
        const double tx = std::tan(s.hfovDeg * acoustic_detail::kPi / 360.0);
        const double ty = tx / std::max(0.1, s.aspect);
        out.w = s.gridW; out.h = s.gridH;
        std::vector<float> power(std::size_t(s.gridW) * s.gridH, 0.0f);
        std::vector<float> er(M), ei(M), rr(M), ri(M);
        for (int gy = 0; gy < s.gridH; gy++)
            for (int gx = 0; gx < s.gridW; gx++) {
                const double dx = ((gx + 0.5) / s.gridW * 2 - 1) * tx, dy = ((gy + 0.5) / s.gridH * 2 - 1) * ty;
                const double inv = 1.0 / std::sqrt(dx * dx + dy * dy + 1.0);
                const double ux = dx * inv, uy = dy * inv;
                // steering phase of mic m at bin k: -2 pi f_k tau_m, tau_m = p_m . d / c; advanced bin by bin by a rotation
                for (int m = 0; m < M; m++) {
                    const double tau = (double(geo[std::size_t(m)].x) * ux + double(geo[std::size_t(m)].y) * uy) * 1e-3 / s.speedOfSound;
                    const double a0 = -2.0 * acoustic_detail::kPi * (kLo * df) * tau, da = -2.0 * acoustic_detail::kPi * df * tau;
                    er[std::size_t(m)] = float(std::cos(a0)); ei[std::size_t(m)] = float(std::sin(a0));
                    rr[std::size_t(m)] = float(std::cos(da)); ri[std::size_t(m)] = float(std::sin(da));
                }
                float acc = 0;
                for (int k = 0; k < nb; k++) {
                    for (int sg = 0; sg < nseg; sg++) {
                        const float* pr = &xr[(std::size_t(k) * nseg + std::size_t(sg)) * M];
                        const float* pi = &xi[(std::size_t(k) * nseg + std::size_t(sg)) * M];
                        float sr = 0, si = 0;
                        for (int m = 0; m < M; m++) {
                            sr += pr[m] * er[std::size_t(m)] - pi[m] * ei[std::size_t(m)];
                            si += pr[m] * ei[std::size_t(m)] + pi[m] * er[std::size_t(m)];
                        }
                        acc += sr * sr + si * si;
                    }
                    for (int m = 0; m < M; m++) {          // next bin
                        const float nr = er[std::size_t(m)] * rr[std::size_t(m)] - ei[std::size_t(m)] * ri[std::size_t(m)];
                        ei[std::size_t(m)] = er[std::size_t(m)] * ri[std::size_t(m)] + ei[std::size_t(m)] * rr[std::size_t(m)];
                        er[std::size_t(m)] = nr;
                    }
                }
                power[std::size_t(gy) * s.gridW + gx] = acc;
            }

        out.power = std::move(power);
        out.tanX = float(tx); out.tanY = float(ty);
        out.baselineApplied = false; out.snrDb = 0;
        acoustic_detail::finishMap(out);
        // mean power per microphone and bin, from the window-compensated spectra (Hann: sum w^2 = 3/8 N), dB re (2^23)^2
        const double perMic = levelSum / (double(nseg) * nb * M) / (0.375 * kFft * kFft) * 2.0;
        out.levelDb = float(10.0 * std::log10(std::max(perMic / (8388608.0 * 8388608.0), 1e-30)));
        out.computeMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        return true;
    }

private:
    double m_fs;
    std::vector<float> m_window;
};

// ---- colours of the map ---------------------------------------------------------------------------------------------------

// RGBA (w * h * 4 bytes) of the map: the top `rangeDb` below the maximum are coloured (blue -> cyan -> green -> yellow -> red),
// the rest is transparent.
inline std::vector<uint8_t> colorizeMap(const AcousticMap& m, double rangeDb) {
    std::vector<uint8_t> rgba(std::size_t(m.w) * m.h * 4, 0);
    for (std::size_t i = 0; i < m.db.size(); i++) {
        const double t = std::max(0.0, std::min(1.0, 1.0 + double(m.db[i]) / std::max(0.5, rangeDb)));
        if (t <= 0.0) continue;
        double r, g, b;
        if (t < 0.25) { r = 0; g = t / 0.25; b = 1; }
        else if (t < 0.5) { r = 0; g = 1; b = 1 - (t - 0.25) / 0.25; }
        else if (t < 0.75) { r = (t - 0.5) / 0.25; g = 1; b = 0; }
        else { r = 1; g = 1 - (t - 0.75) / 0.25; b = 0; }
        rgba[i * 4 + 0] = uint8_t(255 * r); rgba[i * 4 + 1] = uint8_t(255 * g); rgba[i * 4 + 2] = uint8_t(255 * b);
        rgba[i * 4 + 3] = uint8_t(200 * std::pow(t, 1.5));          // transparent at the floor, fairly opaque at the peak
    }
    return rgba;
}

// ---- baseline: the sound of the environment ------------------------------------------------------------------------------------

// Average beamformed power of the environment alone (fans, ventilation, a machine that is always there...), per direction. It is
// only valid for the settings it was measured with (band, field of view, grid, flips): changing one discards it.
struct AcousticBaseline {
    bool valid = false;
    AcousticSettings settings;
    int w = 0, h = 0;
    int count = 0;                       // maps averaged
    std::vector<float> power;
};

inline bool sameBeamSettings(const AcousticSettings& a, const AcousticSettings& b) {
    return a.fLo == b.fLo && a.fHi == b.fHi && a.hfovDeg == b.hfovDeg && a.aspect == b.aspect && a.gridW == b.gridW &&
           a.gridH == b.gridH && a.speedOfSound == b.speedOfSound && a.flipX == b.flipX && a.flipY == b.flipY;
}

// What the live map has in excess of the baseline: power - k * baseline (k = margin, e.g. 3 dB = 2x), negative parts cut. A map
// of the environment alone gives a blank map (empty); a new source shows alone, even if a louder one was already there.
inline void subtractBaseline(const AcousticMap& live, const AcousticBaseline& base, double marginDb, AcousticMap& out) {
    out = live;
    if (!base.valid || base.power.size() != live.power.size()) return;
    const float k = float(std::pow(10.0, marginDb / 10.0));
    std::size_t lp = 0;
    for (std::size_t i = 1; i < live.power.size(); i++) if (live.power[i] > live.power[lp]) lp = i;
    for (std::size_t i = 0; i < out.power.size(); i++) out.power[i] = std::max(0.0f, live.power[i] - k * base.power[i]);
    acoustic_detail::finishMap(out);
    out.baselineApplied = true;
    // how far the strongest place of the excess is above the environment there
    std::size_t ep = 0;
    for (std::size_t i = 1; i < out.power.size(); i++) if (out.power[i] > out.power[ep]) ep = i;
    out.snrDb = float(10.0 * std::log10(std::max(live.power[ep], 1e-30f) / std::max(base.power[ep], 1e-30f)));
    (void)lp;
}

// ---- background worker ----------------------------------------------------------------------------------------------------

class AcousticWorker {
public:
    explicit AcousticWorker(std::shared_ptr<AudioCapture> audio) : m_audio(std::move(audio)), m_beam(m_audio->sampleRate()) {
        m_thread = std::thread([this] { run(); });
    }
    ~AcousticWorker() {
        {
            std::lock_guard<std::mutex> l(m_mx);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    AcousticWorker(const AcousticWorker&) = delete;
    AcousticWorker& operator=(const AcousticWorker&) = delete;

    void setSettings(const AcousticSettings& s) { std::lock_guard<std::mutex> l(m_mx); m_settings = s; }

    // Baseline: averages the next `maps` maps (make sure only the environment sounds meanwhile). Needs the same settings until done.
    void captureBaseline(int maps = 12) {
        std::lock_guard<std::mutex> l(m_mx);
        m_capTotal = std::max(1, maps);
        m_capCount = 0;
        m_capSettings = m_settings;
        m_capSum.clear();
        m_baseDiscarded = false;
    }
    void clearBaseline() {
        std::lock_guard<std::mutex> l(m_mx);
        m_base = AcousticBaseline();
        m_capTotal = 0;
        m_baseDiscarded = false;
    }
    // With a valid baseline: publish what exceeds it (margin in dB) instead of the raw map.
    void setSubtract(bool on, double marginDb) {
        std::lock_guard<std::mutex> l(m_mx);
        m_subtract = on;
        m_marginDb = marginDb;
    }
    struct BaselineState {
        bool valid = false, capturing = false, discarded = false;
        int collected = 0, total = 0;
    };
    BaselineState baselineState() const {
        std::lock_guard<std::mutex> l(m_mx);
        BaselineState b;
        b.valid = m_base.valid;
        b.capturing = m_capTotal > 0;
        b.discarded = m_baseDiscarded;
        b.collected = m_capCount;
        b.total = m_capTotal;
        return b;
    }
    void setRate(double hz) { std::lock_guard<std::mutex> l(m_mx); m_intervalMs = hz > 0 ? 1000.0 / hz : 0.0; m_cv.notify_all(); }
    AcousticMap latest() const { std::lock_guard<std::mutex> l(m_mx); return m_map; }
    // "" when computing; otherwise why nothing is computed (no continuous audio yet...).
    std::string status() const { std::lock_guard<std::mutex> l(m_mx); return m_status; }

private:
    void run() {
        std::vector<int32_t> buf(Beamformer::kFramesNeeded * kAudioChannels);
        uint64_t lastEnd = ~uint64_t(0);
        for (;;) {
            AcousticSettings s;
            double interval;
            {
                std::unique_lock<std::mutex> l(m_mx);
                if (m_stop) return;
                s = m_settings;
                interval = m_intervalMs;
            }
            uint64_t end = 0;
            const std::size_t n = m_audio->recentFrames(Beamformer::kFramesNeeded, buf.data(), &end);
            std::string status;
            if (n < std::size_t(Beamformer::kFft)) {
                status = "waiting for continuous audio (microphones running?)";
            } else if (end == lastEnd) {
                status = "no new audio (microphones stopped?)";
            } else {
                AcousticMap map;
                if (m_beam.compute(buf.data(), n, s, map)) {
                    lastEnd = end;
                    map.endFrame = end;
                    std::lock_guard<std::mutex> l(m_mx);
                    // baseline capture in progress: average the raw powers
                    if (m_capTotal > 0) {
                        if (!sameBeamSettings(m_capSettings, s)) {            // a setting changed meanwhile: start over
                            m_capSettings = s;
                            m_capCount = 0;
                            m_capSum.clear();
                        }
                        if (m_capSum.empty()) m_capSum.assign(map.power.size(), 0.0);
                        for (std::size_t i = 0; i < map.power.size() && i < m_capSum.size(); i++) m_capSum[i] += map.power[i];
                        if (++m_capCount >= m_capTotal) {
                            m_base.valid = true;
                            m_base.settings = s;
                            m_base.w = map.w; m_base.h = map.h; m_base.count = m_capCount;
                            m_base.power.resize(m_capSum.size());
                            for (std::size_t i = 0; i < m_capSum.size(); i++) m_base.power[i] = float(m_capSum[i] / m_capCount);
                            m_capTotal = 0;
                            m_baseDiscarded = false;
                        }
                    }
                    if (m_base.valid && !sameBeamSettings(m_base.settings, s)) {   // measured with other settings: no longer usable
                        m_base.valid = false;
                        m_baseDiscarded = true;
                    }
                    const uint64_t seq = m_map.sequence + 1;
                    if (m_subtract && m_base.valid) subtractBaseline(map, m_base, m_marginDb, m_map);
                    else m_map = std::move(map);
                    m_map.sequence = seq;
                } else {
                    status = "band outside the analysable range";
                }
            }
            std::unique_lock<std::mutex> l(m_mx);
            m_status = status;
            m_cv.wait_for(l, std::chrono::duration<double, std::milli>(std::max(20.0, interval)), [this] { return m_stop; });
            if (m_stop) return;
        }
    }

    std::shared_ptr<AudioCapture> m_audio;
    Beamformer m_beam;
    mutable std::mutex m_mx;
    std::condition_variable m_cv;
    AcousticSettings m_settings;
    double m_intervalMs = 200;            // 5 maps per second
    AcousticMap m_map;
    AcousticBaseline m_base;
    bool m_subtract = false, m_baseDiscarded = false;
    double m_marginDb = 3.0;
    int m_capTotal = 0, m_capCount = 0;
    AcousticSettings m_capSettings;
    std::vector<double> m_capSum;
    std::string m_status;
    bool m_stop = false;
    std::thread m_thread;
};

}  // namespace okcli
