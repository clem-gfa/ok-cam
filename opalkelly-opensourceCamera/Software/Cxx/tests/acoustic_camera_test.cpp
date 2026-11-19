// Test hors materiel de AcousticCamera.h : FFT, geometrie, formation de voies sur des sources synthetiques (direction retrouvee,
// bon sens gauche/droite/haut/bas), couleurs, worker alimente par un AudioCapture simule (paquets continus).
// Build : g++ -std=c++17 -O2 -I../src acoustic_camera_test.cpp -pthread -o acoustic_camera_test
#include <cassert>
#include <chrono>
#include <cmath>
#include <deque>
#include <iostream>
#include <random>
#include <thread>

#include "AcousticCamera.h"

using namespace okcli;

static const double FS = 189318.0;       // 99.96e6 / 528
static const double C = 343.0;

// 64-channel signal of a plane wave arriving from direction (xn, yn) of the image (normalised coordinates), multi-tone in the
// band, plus independent noise: x_m(t) = s(t + p_m . d / c)
static std::vector<int32_t> synth(std::size_t frames, double xn, double yn, const AcousticSettings& s, double noiseAmp, bool flipX = false,
                                  bool flipY = false, std::size_t startFrame = 0) {
    const auto geo = darwinMicGeometry(flipX, flipY);
    const double tx = std::tan(s.hfovDeg * acoustic_detail::kPi / 360.0), ty = tx / s.aspect;
    double dx = xn * tx, dy = yn * ty, dz = 1.0;
    const double nrm = std::sqrt(dx * dx + dy * dy + dz * dz);
    dx /= nrm; dy /= nrm;
    const double freqs[] = {9100, 10900, 13300, 15700, 17900};
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, noiseAmp);
    std::vector<int32_t> out(frames * 64);
    for (std::size_t i = 0; i < frames; i++)
        for (int m = 0; m < 64; m++) {
            const double tau = (geo[std::size_t(m)].x * dx + geo[std::size_t(m)].y * dy) * 1e-3 / C;
            const double t = double(startFrame + i) / FS + tau;
            double v = 0;
            for (int k = 0; k < 5; k++) v += std::sin(2 * acoustic_detail::kPi * freqs[k] * t + k);
            out[i * 64 + std::size_t(m)] = int32_t(std::lround(v * 400000.0 + noise(rng)));
        }
    return out;
}

static void cellOf(double xn, double yn, const AcousticSettings& s, int& gx, int& gy) {
    gx = int((xn + 1) / 2 * s.gridW);
    gy = int((yn + 1) / 2 * s.gridH);
}

int main() {
    // ---- FFT against a direct DFT
    {
        const int n = 64;
        std::vector<float> re(n), im(n, 0.0f), dre(n), dim(n);
        for (int i = 0; i < n; i++) re[i] = float(std::sin(0.3 * i) + 0.5 * std::cos(1.7 * i));
        for (int k = 0; k < n; k++) {
            double a = 0, b = 0;
            for (int i = 0; i < n; i++) { a += re[i] * std::cos(2 * acoustic_detail::kPi * k * i / n); b -= re[i] * std::sin(2 * acoustic_detail::kPi * k * i / n); }
            dre[k] = float(a); dim[k] = float(b);
        }
        acoustic_detail::fft(re.data(), im.data(), n);
        for (int k = 0; k < n; k++) assert(std::fabs(re[k] - dre[k]) < 1e-3 && std::fabs(im[k] - dim[k]) < 1e-3);
    }
    // ---- geometry: camera frame
    {
        const auto g = darwinMicGeometry();
        // MK39 (KiCad 99.38, 107.04) is just below-left of the hole in the top view: x = y_kicad - 100 = +7.04, y = 100 - x_kicad = +0.62
        assert(std::fabs(g[38].x - 7.04f) < 0.01f && std::fabs(g[38].y - 0.62f) < 0.01f);
        const auto f = darwinMicGeometry(true, true);
        assert(std::fabs(f[38].x + 7.04f) < 0.01f && std::fabs(f[38].y + 0.62f) < 0.01f);
        float cx = 0, cy = 0, rmax = 0;
        for (const auto& p : g) { cx += p.x; cy += p.y; rmax = std::max(rmax, std::hypot(p.x, p.y)); }
        assert(std::fabs(cx / 64) < 8 && std::fabs(cy / 64) < 8 && rmax > 30 && rmax < 50);     // spiral around the hole, radius ~ 40 mm
    }
    // ---- beamforming: the direction of a source is found, with the right orientation
    {
        AcousticSettings s;
        Beamformer bf(FS);
        const double dirs[][2] = {{0, 0}, {0.6, 0}, {-0.6, 0}, {0, 0.6}, {0, -0.6}, {0.5, 0.5}, {-0.7, -0.4}};
        for (const auto& d : dirs) {
            const auto sig = synth(Beamformer::kFramesNeeded, d[0], d[1], s, 20000);
            AcousticMap map;
            assert(bf.compute(sig.data(), Beamformer::kFramesNeeded, s, map));
            int gx, gy; cellOf(d[0], d[1], s, gx, gy);
            std::cout << "source (" << d[0] << ", " << d[1] << ") -> cell (" << map.peakX << ", " << map.peakY << "), expected (" << gx << ", " << gy
                      << "), contrast " << map.contrastDb << " dB, " << map.computeMs << " ms\n";
            assert(std::abs(map.peakX - gx) <= 1 && std::abs(map.peakY - gy) <= 1);
            assert(map.contrastDb > 3.0f && map.w == s.gridW && map.h == s.gridH && map.db[std::size_t(map.peakY) * map.w + map.peakX] == 0.0f);
            if (d[0] > 0.3) assert(map.azimuthDeg > 5);           // right of the image: positive azimuth
            if (d[0] < -0.3) assert(map.azimuthDeg < -5);
            if (d[1] < -0.3) assert(map.elevationDeg > 3);        // up in the image: positive elevation
            if (d[1] > 0.3) assert(map.elevationDeg < -3);
        }
        // a board mounted mirrored shows a mirrored picture: flipX moves the peak to the other side
        const auto sig = synth(Beamformer::kFramesNeeded, 0.6, 0, s, 20000);
        AcousticMap a, b;
        AcousticSettings sf = s; sf.flipX = true;
        assert(bf.compute(sig.data(), Beamformer::kFramesNeeded, s, a) && bf.compute(sig.data(), Beamformer::kFramesNeeded, sf, b));
        assert(a.peakX > s.gridW / 2 && b.peakX < s.gridW / 2);
        // not enough signal / empty band
        AcousticMap e;
        assert(!bf.compute(sig.data(), 100, s, e));
        AcousticSettings bad = s; bad.fLo = 60000; bad.fHi = 50000;
        assert(!bf.compute(sig.data(), Beamformer::kFramesNeeded, bad, e));
        // level: 5 tones of amplitude 4e5 (power 5 * 8e10 / 2 / (2^23)^2) = -33 dB re full scale squared, on all microphones
        AcousticMap lv; bf.compute(sig.data(), Beamformer::kFramesNeeded, s, lv);
        assert(lv.levelDb > -50 && lv.levelDb < -20);
    }
    // ---- a source in the higher band (35 kHz): sharper map, same direction
    {
        AcousticSettings s; s.fLo = 30000; s.fHi = 40000;
        Beamformer bf(FS);
        // tones inside 30-40 kHz: build the source with other frequencies by scaling time
        const auto geo = darwinMicGeometry();
        const double tx = std::tan(s.hfovDeg * acoustic_detail::kPi / 360.0), ty = tx / s.aspect;
        double dx = 0.4 * tx, dy = -0.3 * ty, nrm = std::sqrt(dx * dx + dy * dy + 1);
        dx /= nrm; dy /= nrm;
        std::vector<int32_t> sig(Beamformer::kFramesNeeded * 64);
        for (std::size_t i = 0; i < Beamformer::kFramesNeeded; i++)
            for (int m = 0; m < 64; m++) {
                const double t = double(i) / FS + (geo[m].x * dx + geo[m].y * dy) * 1e-3 / C;
                sig[i * 64 + m] = int32_t(std::lround(500000.0 * (std::sin(2 * acoustic_detail::kPi * 33000 * t) + std::sin(2 * acoustic_detail::kPi * 36500 * t + 1))));
            }
        AcousticMap map;
        assert(bf.compute(sig.data(), sig.size() / 64, s, map));
        int gx, gy; cellOf(0.4, -0.3, s, gx, gy);
        std::cout << "35 kHz source -> cell (" << map.peakX << ", " << map.peakY << "), expected (" << gx << ", " << gy << ")\n";
        assert(std::abs(map.peakX - gx) <= 1 && std::abs(map.peakY - gy) <= 1);
    }
    // ---- colours: transparent below the range, opaque red at the peak
    {
        AcousticMap m; m.w = 3; m.h = 1; m.db = {0.0f, -3.0f, -30.0f};
        const auto c = colorizeMap(m, 6.0);
        assert(c.size() == 12 && c[3] > 150 && c[0] > 200 && c[11] == 0 && c[7] > 0 && c[7] < c[3]);
    }
    // ---- worker fed by an AudioCapture (packets of 21 frames), continuous signal, then a gap
    {
        struct Pipe : IAudioPipe {
            std::deque<std::vector<uint8_t>> q;
            uint32_t available() override { return uint32_t(q.size() * kAudioPacketBytes / 4); }
            void read(uint8_t* b, std::size_t n) override {
                for (std::size_t i = 0; i < n / kAudioPacketBytes; i++) { std::memcpy(b + i * kAudioPacketBytes, q.front().data(), kAudioPacketBytes); q.pop_front(); }
            }
        } pipe;
        AcousticSettings s;
        const std::size_t NP = 80, NS = 21;
        const auto sig = synth(NP * NS, -0.5, 0.3, s, 10000);
        auto packet = [&](std::size_t seq) {
            std::vector<uint8_t> p(kAudioPacketBytes, 0);
            auto put32 = [&](std::size_t w, uint32_t x) { for (int i = 0; i < 4; i++) p[4 * w + std::size_t(i)] = uint8_t(x >> (8 * i)); };
            put32(0, kAudioMagic); put32(1, uint32_t(seq)); put32(4, uint32_t(NS));
            for (std::size_t i = 0; i < NS; i++)
                for (int m = 0; m < 64; m++) {
                    const uint32_t v = uint32_t(sig[(seq * NS + i) * 64 + std::size_t(m)]) & 0xFFFFFF;
                    uint8_t* d = &p[32 + i * 192 + std::size_t(m) * 3];
                    d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); d[2] = uint8_t(v >> 16);
                }
            return p;
        };
        AudioCaptureOptions opt; opt.keepRaw = true; opt.rawFrames = 4096; opt.ringFrames = 8192;
        auto cap = std::make_shared<AudioCapture>(pipe, opt);
        cap->reset();
        for (std::size_t q = 0; q < NP; q++) pipe.q.push_back(packet(q));
        while (cap->pump() > 0) {}
        std::vector<int32_t> tmp(2048 * 64);
        uint64_t end = 0;
        assert(cap->recentFrames(2048, tmp.data(), &end) == 1680 && end == 1680);           // all 80 packets, continuous
        assert(tmp[0] == sig[0] && tmp[63] == sig[63] && tmp[(1680 - 1) * 64 + 5] == sig[(1680 - 1) * 64 + 5]);
        {
            AcousticWorker w(cap);
            w.setRate(50);
            w.setSettings(s);
            for (int i = 0; i < 100 && w.latest().sequence == 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const AcousticMap m = w.latest();
            int gx, gy; cellOf(-0.5, 0.3, s, gx, gy);
            assert(m.sequence >= 1 && std::abs(m.peakX - gx) <= 1 && std::abs(m.peakY - gy) <= 1 && m.endFrame == 1680);
            for (int i = 0; i < 50 && w.status().empty(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            assert(w.status().find("no new audio") != std::string::npos);                   // nothing arrives any more
        }
        // a lost packet breaks the continuity: only the frames after the gap are offered (never a signal with a hole in it)
        Pipe pipe2;
        AudioCapture cap2(pipe2, opt);
        cap2.reset();
        for (std::size_t q = 0; q < NP; q++) if (q != 40 && q != 41) pipe2.q.push_back(packet(q));
        while (cap2.pump() > 0) {}
        assert(cap2.recentFrames(2048, tmp.data(), &end) == (NP - 42) * NS);                // 38 packets after the gap
        assert(cap2.stats().lostPackets == 2);
    }
    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
