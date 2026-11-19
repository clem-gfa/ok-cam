/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AudioConvert.h — reduces the size of the recorded audio: lower sample rate (decimation by 2, 4 or 8, with an anti-aliasing
// filter) and/or fewer bits (16 instead of 24).
//
// The FPGA delivers 64 channels x 24 bit at ~189 kS/s = 36.3 MB/s. Typical results:
//   24 bit, full rate : 36.3 MB/s   (bit-exact copy, no processing)
//   16 bit, full rate : 24.2 MB/s
//   24 bit, / 2       : 18.2 MB/s   (94.7 kS/s, band up to ~38 kHz)
//   16 bit, / 2       : 12.1 MB/s
//   16 bit, / 4       :  6.1 MB/s   (47.3 kS/s, band up to ~19 kHz)
//
// Decimation by 2 is a half-band FIR low-pass (47 taps, Kaiser, passband flat to 0.2 x fs_in, >60 dB attenuation above
// 0.3 x fs_in), half of its coefficients are zero. Decimation by 4 / 8 cascades it. The filter delay (23 samples of each
// stage's input rate) is constant and the first outputs are the start-up transient of the filter. Going to 16 bits rounds to
// the nearest value (no dither): the quantisation noise is -98 dBFS, below the noise of the microphones.
//
// The conversion runs on the writer thread of AudioCapture (never on the acquisition thread) and is streaming: the result
// does not depend on how the input is cut into chunks.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace okcli {

constexpr double kPi = 3.14159265358979323846;   // M_PI is not defined by MSVC without _USE_MATH_DEFINES
constexpr int kAudioFormatChannels = 64;      // same as kAudioChannels (AudioStream.h), repeated so that this file stands alone
constexpr int kAudioFormatFrameBytes = 192;   // 64 x 24 bit

struct AudioRecordFormat {
    int decimation = 1;    // 1, 2, 4 or 8
    int bits = 24;         // 24 or 16

    bool valid() const { return (decimation == 1 || decimation == 2 || decimation == 4 || decimation == 8) && (bits == 24 || bits == 16); }
    bool isCopy() const { return decimation == 1 && bits == 24; }
    int bytesPerSample() const { return bits / 8; }
    int bytesPerFrame() const { return bytesPerSample() * kAudioFormatChannels; }
    double outRate(double inRate) const { return inRate / decimation; }
    double bytesPerSecond(double inRate) const { return outRate(inRate) * bytesPerFrame(); }
};

class AudioConverter {
public:
    explicit AudioConverter(AudioRecordFormat fmt) : m_fmt(fmt) {
        int stages = 0;
        for (int d = fmt.decimation; d > 1; d >>= 1) stages++;
        m_stages.resize(std::size_t(stages));
        for (Stage& s : m_stages) s.hist.assign(std::size_t(kAudioFormatChannels) * (kTaps - 1), 0.0f);
    }

    // `in`: `frames` frames of 64 channels x 24-bit signed little-endian samples (interleaved). Appends the converted frames
    // (64 channels x 16 or 24 bits, interleaved) to `out` and returns their number.
    std::size_t process(const uint8_t* in, std::size_t frames, std::vector<uint8_t>& out) {
        if (m_fmt.isCopy()) {
            out.insert(out.end(), in, in + frames * kAudioFormatFrameBytes);
            return frames;
        }
        const int C = kAudioFormatChannels;
        // planar float, one vector per channel
        std::vector<std::vector<float>> cur(C, std::vector<float>(frames));
        for (std::size_t i = 0; i < frames; i++) {
            const uint8_t* f = in + i * kAudioFormatFrameBytes;
            for (int c = 0; c < C; c++) {
                const int32_t v = (int32_t(uint32_t(f[3 * c]) | uint32_t(f[3 * c + 1]) << 8 | uint32_t(f[3 * c + 2]) << 16) << 8) >> 8;
                cur[c][i] = float(v);
            }
        }
        for (Stage& st : m_stages) {
            int phaseOut = st.phase;
            for (int c = 0; c < C; c++) cur[c] = st.run(c, cur[c], phaseOut);
            st.phase = phaseOut;
        }
        const std::size_t n = cur[0].size();
        const std::size_t base = out.size();
        out.resize(base + n * std::size_t(m_fmt.bytesPerFrame()));
        uint8_t* o = out.data() + base;
        for (std::size_t i = 0; i < n; i++)
            for (int c = 0; c < C; c++) {
                const float x = cur[c][i];
                if (m_fmt.bits == 24) {
                    const int32_t v = std::max(-8388608, std::min(8388607, int32_t(std::lrintf(x))));
                    *o++ = uint8_t(v); *o++ = uint8_t(v >> 8); *o++ = uint8_t(v >> 16);
                } else {
                    const int32_t v = std::max(-32768, std::min(32767, int32_t(std::lrintf(x / 256.0f))));
                    *o++ = uint8_t(v); *o++ = uint8_t(v >> 8);
                }
            }
        return n;
    }

private:
    static constexpr int kTaps = 47;                 // half-band: taps at even offsets from the centre are zero

    // Coefficients (symmetric), computed once: windowed sinc of cutoff fs_in/4, Kaiser window.
    static const std::vector<float>& coef() {
        static const std::vector<float> h = [] {
            std::vector<double> d(kTaps);
            const double beta = 7.0;
            auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; k++) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
            double sum = 0;
            for (int k = 0; k < kTaps; k++) {
                const double n = k - (kTaps - 1) / 2.0;
                const double sinc = n == 0 ? 0.5 : std::sin(kPi * n / 2.0) / (kPi * n);       // cutoff 1/4 of the sampling rate
                const double r = 2.0 * k / (kTaps - 1) - 1.0;
                d[k] = sinc * i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
                sum += d[k];
            }
            std::vector<float> f(kTaps);
            for (int k = 0; k < kTaps; k++) f[k] = float(d[k] / sum);   // unity gain at DC
            return f;
        }();
        return h;
    }

    struct Stage {
        std::vector<float> hist;     // per channel: the last kTaps-1 input samples
        int phase = 0;               // input samples to skip before the next output (0 or 1)

        // One channel through the stage. `phaseOut` receives the phase for the next chunk (the same for every channel).
        std::vector<float> run(int c, const std::vector<float>& x, int& phaseOut) {
            const std::vector<float>& h = coef();
            const std::size_t H = kTaps - 1;
            std::vector<float> s(H + x.size());
            std::memcpy(s.data(), &hist[std::size_t(c) * H], H * sizeof(float));
            std::copy(x.begin(), x.end(), s.begin() + std::ptrdiff_t(H));
            std::vector<float> y;
            y.reserve(x.size() / 2 + 1);
            std::size_t e = H + std::size_t(phase);                 // index (in s) of the newest sample of the window
            for (; e < s.size(); e += 2) {
                float acc = h[kTaps / 2] * s[e - kTaps / 2];
                for (int k = 0; k < kTaps / 2; k += 2)               // nonzero taps (odd distance from the centre), symmetric pairs
                    acc += h[k] * (s[e - k] + s[e - (kTaps - 1) + k]);
                y.push_back(acc);
            }
            std::memcpy(&hist[std::size_t(c) * H], s.data() + s.size() - H, H * sizeof(float));
            phaseOut = int(e - s.size());
            return y;
        }
    };

    AudioRecordFormat m_fmt;
    std::vector<Stage> m_stages;
};

}  // namespace okcli
