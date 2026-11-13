// Test hors materiel de AudioStream.h : flux synthetique (paquets avec trous), pipe simulee, fichier WAV RF64.
// Build : g++ -std=c++17 -I../src audio_stream_test.cpp -pthread -o audio_stream_test
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>

#include "AudioStream.h"

using namespace okcli;

static void put32(std::vector<uint8_t>& v, std::size_t wordIdx, uint32_t x) {
    for (int i = 0; i < 4; i++) v[4 * wordIdx + i] = uint8_t(x >> (8 * i));
}

// Echantillon du canal c a l'instant global g : valeur signee sur 24 bits, motif reconnaissable.
static int32_t sampleValue(uint64_t g, int c) {
    int32_t v = int32_t((g * 977 + c * 131) % 16000000) - 8000000;
    return v;
}

static std::vector<uint8_t> makePacket(uint32_t seq, uint64_t ts, uint32_t vframes, uint64_t vts, bool lost) {
    std::vector<uint8_t> p(kAudioPacketBytes, 0);
    put32(p, 0, kAudioMagic); put32(p, 1, seq); put32(p, 2, uint32_t(ts)); put32(p, 3, uint32_t(ts >> 32));
    put32(p, 4, 21 | (lost ? 1u << 16 : 0)); put32(p, 5, vframes); put32(p, 6, uint32_t(vts)); put32(p, 7, uint32_t(vts >> 32));
    for (int i = 0; i < 21; i++)
        for (int c = 0; c < 64; c++) {
            const uint32_t v = uint32_t(sampleValue(uint64_t(seq) * 21 + i, c)) & 0xFFFFFF;
            uint8_t* d = &p[32 + i * 192 + c * 3];
            d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); d[2] = uint8_t(v >> 16);
        }
    return p;
}

class FakePipe : public IAudioPipe {
public:
    std::deque<uint8_t> q;
    bool finished = false;
    void push(const std::vector<uint8_t>& p) { q.insert(q.end(), p.begin(), p.end()); }
    void read(uint8_t* buf, std::size_t n) override {
        if (q.size() < n) {              // plus de donnees : simule la fin (le vrai materiel attendrait)
            finished = true;
            throw std::runtime_error("end of data");
        }
        for (std::size_t i = 0; i < n; i++) { buf[i] = q.front(); q.pop_front(); }
    }
};

int main() {
    FakePipe pipe;
    // 48 paquets, seq 0..47 sauf 10, 11 et 30 (3 paquets perdus), multiple de 16 apres retrait
    const int N = 48;
    int sent = 0;
    for (uint32_t s = 0; s < uint32_t(N); s++) {
        if (s == 10 || s == 11 || s == 30) continue;
        const bool afterGap = (s == 12 || s == 31);
        pipe.push(makePacket(s, 1000000ull + s * 21ull * 480, uint32_t(s / 10), 500000ull + (s / 10) * 3000000ull, afterGap));
        sent++;
    }
    // completer a un multiple de 16 paquets (taille de lecture) avec des paquets valides en queue
    uint32_t extra = N;
    while (sent % 16) { pipe.push(makePacket(extra, 1000000ull + extra * 21ull * 480, 9, 0, false)); extra++; sent++; }

    const std::string path = "audio_test.wav";
    AudioCapture cap(pipe, 95.5e6, 5000);
    cap.start(path);
    for (int i = 0; i < 200 && !pipe.finished; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cap.stop();

    const AudioStats st = cap.stats();
    std::cout << "paquets=" << st.packets << " perdus=" << st.lostPackets << " frames=" << st.frames << " erreur='" << cap.error() << "'\n";
    assert(st.packets == uint64_t(sent));
    assert(st.lostPackets == 3);
    assert(st.badBlocks == 0);
    assert(st.frames == uint64_t(extra) * 21);       // perdus remplaces par du silence : duree = tous les paquets

    // fichier WAV RF64
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> w((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    assert(std::memcmp(&w[0], "RF64", 4) == 0 && std::memcmp(&w[8], "WAVE", 4) == 0 && std::memcmp(&w[12], "ds64", 4) == 0);
    const std::size_t dataOff = 12 + 8 + 28 + 8 + 40 + 8;
    assert(std::memcmp(&w[dataOff - 8], "data", 4) == 0);
    assert(w.size() == dataOff + st.frames * 192);
    assert(w[56] == 0xFE && w[57] == 0xFF);                       // WAVE_FORMAT_EXTENSIBLE
    assert(w[58] == 64 && w[59] == 0);                            // 64 canaux
    const uint32_t rate = audioWord(&w[0], 15);                  // octets 60..63
    assert(rate == 198958);                                       // 95,5 MHz / 480
    assert(w[70] == 24 && w[74] == 24);                          // bits par echantillon, bits valides
    const uint64_t dsData = uint64_t(audioWord(&w[0], 7)) | uint64_t(audioWord(&w[0], 8)) << 32;   // ds64 : taille des donnees
    assert(dsData == st.frames * 192);
    // contenu : verifie chaque instant (silence pour les perdus)
    auto lostSeq = [](uint32_t s) { return s == 10 || s == 11 || s == 30; };
    for (uint32_t s = 0; s < extra; s++)
        for (int i = 0; i < 21; i++)
            for (int c = 0; c < 64; c += 7) {
                const uint8_t* d = &w[dataOff + (uint64_t(s) * 21 + i) * 192 + c * 3];
                int32_t v = int32_t(d[0] | d[1] << 8 | d[2] << 16); v = (v << 8) >> 8;
                const int32_t exp = lostSeq(s) ? 0 : sampleValue(uint64_t(s) * 21 + i, c);
                if (v != exp) { std::cerr << "mismatch s=" << s << " i=" << i << " c=" << c << " got " << v << " exp " << exp << "\n"; return 1; }
            }
    // CSV : une ligne par paquet recu + en-tete ; flag perdu aux paquets 12 et 31
    std::ifstream cf(path + ".csv"); std::string line; int lines = 0, flagged = 0;
    while (std::getline(cf, line)) { lines++; if (!line.empty() && line.back() == '1' && lines > 1) flagged++; }
    std::cout << "csv lignes=" << lines << " perdus signales=" << flagged << "\n";
    assert(lines == sent + 1 && flagged == 2);
    // fenetre d'affichage : derniers echantillons du canal 5
    const auto rec = cap.recent(5, 100);
    assert(rec.size() == 100);
    const uint64_t lastG = uint64_t(extra) * 21 - 1;
    assert(rec.back() == sampleValue(lastG, 5));
    std::cout << "TOUS LES TESTS OK\n";
    if (!std::getenv("KEEP")) { std::remove(path.c_str()); std::remove((path + ".csv").c_str()); }
    return 0;
}
