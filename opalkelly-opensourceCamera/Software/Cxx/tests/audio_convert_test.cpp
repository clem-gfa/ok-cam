// Test hors materiel de AudioConvert.h : copie exacte, 16 bits, decimation (gain, repliement), streaming independant du decoupage.
// Build : g++ -std=c++17 -I../src audio_convert_test.cpp -o audio_convert_test
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "AudioConvert.h"

using namespace okcli;

static const double FS = 189318.0;

// frames x 64 channels, 24 bit: channel 0 = tone f0 (amplitude a), channel 1 = tone f1, channel 2 = constant, others silent
static std::vector<uint8_t> makeInput(std::size_t n, double f0, double a0, double f1, double a1, int dc) {
    std::vector<uint8_t> v(n * 192, 0);
    auto put = [&](std::size_t i, int c, int32_t x) { v[i * 192 + 3 * c] = uint8_t(x); v[i * 192 + 3 * c + 1] = uint8_t(x >> 8); v[i * 192 + 3 * c + 2] = uint8_t(x >> 16); };
    for (std::size_t i = 0; i < n; i++) {
        put(i, 0, int32_t(std::lround(a0 * std::sin(2 * kPi * f0 * double(i) / FS))));
        put(i, 1, int32_t(std::lround(a1 * std::sin(2 * kPi * f1 * double(i) / FS))));
        put(i, 2, dc);
    }
    return v;
}
static int32_t get(const std::vector<uint8_t>& o, std::size_t i, int c, int bytes) {
    const uint8_t* p = &o[(i * 64 + c) * bytes];
    return bytes == 3 ? (int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16) << 8) >> 8
                      : int32_t(int16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8));
}
// amplitude of the tone f (Hz, at sampling rate fs) in channel c, skipping the filter's start-up
static double amp(const std::vector<uint8_t>& o, std::size_t n, int c, int bytes, double f, double fs, double scale) {
    double re = 0, im = 0; std::size_t cnt = 0;
    for (std::size_t i = 200; i < n; i++, cnt++) {
        const double x = double(get(o, i, c, bytes)) * scale;
        re += x * std::cos(2 * kPi * f * double(i) / fs); im += x * std::sin(2 * kPi * f * double(i) / fs);
    }
    return 2.0 * std::sqrt(re * re + im * im) / double(cnt);
}

int main() {
    const std::size_t N = 40000;
    const auto in = makeInput(N, 10000, 4e6, 60000, 4e6, 123456);

    // ---- 24 bit, no decimation: bit-exact copy
    {
        AudioConverter c({1, 24}); std::vector<uint8_t> out;
        assert(c.process(in.data(), N, out) == N && out == in);
    }
    // ---- 16 bit, no decimation: nearest 16-bit value of each sample
    {
        AudioConverter c({1, 16}); std::vector<uint8_t> out;
        assert(c.process(in.data(), N, out) == N && out.size() == N * 128);
        for (std::size_t i = 0; i < N; i += 97)
            for (int ch : {0, 1, 2}) {
                const double want = double(get(in, i, ch, 3)) / 256.0;
                assert(std::fabs(double(get(out, i, ch, 2)) - want) <= 0.5 + 1e-9);
            }
        assert(get(out, 5, 7, 2) == 0);
    }
    // ---- decimation by 2, 24 bit: 10 kHz kept (gain 1 +- 0.05 dB), 60 kHz (above the new Nyquist) strongly attenuated, DC kept
    {
        AudioConverter c({2, 24}); std::vector<uint8_t> out;
        const std::size_t n = c.process(in.data(), N, out);
        assert(n >= N / 2 - 1 && n <= N / 2 + 1 && out.size() == n * 192);
        const double a10 = amp(out, n, 0, 3, 10000, FS / 2, 1.0);
        assert(std::fabs(20 * std::log10(a10 / 4e6)) < 0.05);
        // the 60 kHz tone folds to 94.66 - 60 = 34.66 kHz
        const double a60 = amp(out, n, 1, 3, FS / 2 - 60000, FS / 2, 1.0);
        assert(20 * std::log10(a60 / 4e6) < -60);
        double dc = 0; for (std::size_t i = 500; i < n; i++) dc += get(out, i, 2, 3); dc /= double(n - 500);
        assert(std::fabs(dc - 123456) < 2);
    }
    // ---- decimation by 4, 16 bit: 5 kHz kept, 30 kHz (above 23.6 kHz) removed
    {
        const auto in4 = makeInput(N, 5000, 4e6, 30000, 4e6, 0);
        AudioConverter c({4, 16}); std::vector<uint8_t> out;
        const std::size_t n = c.process(in4.data(), N, out);
        assert(n >= N / 4 - 1 && n <= N / 4 + 1 && out.size() == n * 128);
        assert(std::fabs(20 * std::log10(amp(out, n, 0, 2, 5000, FS / 4, 256.0) / 4e6)) < 0.1);
        assert(20 * std::log10(amp(out, n, 1, 2, FS / 4 - 30000, FS / 4, 256.0) / 4e6) < -55);
    }
    // ---- streaming: any way of cutting the input gives the same output
    for (const AudioRecordFormat f : {AudioRecordFormat{2, 24}, AudioRecordFormat{4, 16}, AudioRecordFormat{8, 24}}) {
        AudioConverter whole(f); std::vector<uint8_t> a;
        const std::size_t na = whole.process(in.data(), N, a);
        AudioConverter pieces(f); std::vector<uint8_t> b;
        std::size_t nb = 0, pos = 0; int k = 0;
        const std::size_t cuts[] = {1, 21, 5461, 3, 977, 21, 2};
        while (pos < N) { const std::size_t m = std::min(N - pos, cuts[k++ % 7]); nb += pieces.process(&in[pos * 192], m, b); pos += m; }
        assert(na == nb && a == b);
    }
    assert(AudioRecordFormat({4, 16}).bytesPerSecond(FS) < 6.2e6 && AudioRecordFormat({1, 24}).bytesPerSecond(FS) > 36.2e6);
    assert(!AudioRecordFormat({3, 24}).valid() && !AudioRecordFormat({2, 20}).valid() && AudioRecordFormat({8, 16}).valid());
    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
