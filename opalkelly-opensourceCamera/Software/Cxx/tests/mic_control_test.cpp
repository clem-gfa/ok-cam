// Test hors materiel de AudioPipe.h (usage de l'API FrontPanel par la pipe 0xA1 et le controle des micros), avec un
// faux port de donnees. Build : g++ -std=c++17 -Itests/stub -Isrc tests/mic_control_test.cpp -pthread
#include <cassert>
#include <iostream>
#include <map>
#include <vector>

#include "AudioPipe.h"

using namespace okcli;

class FakePort : public OpalKelly::FPGADataPortClassic {
public:
    std::map<int, unsigned long> wireIn, wireOut;
    std::vector<unsigned long> controlHistory;   // each UpdateWireIns of WireIn 0x01
    int updateOuts = 0;
    long pipeResult = 0;
    int lastEp = 0, lastBlock = 0; long lastLen = 0;
    int SetWireInValue(int ep, unsigned long val, unsigned long mask) override { wireIn[ep] = (wireIn[ep] & ~mask) | (val & mask); return 0; }
    int UpdateWireIns() override { controlHistory.push_back(wireIn[kMicWireInControl]); return 0; }
    int UpdateWireOuts() override { updateOuts++; return 0; }
    unsigned long GetWireOutValue(int ep) override { return wireOut[ep]; }
    long ReadFromBlockPipeOut(int ep, int block, long len, unsigned char* data) override {
        lastEp = ep; lastBlock = block; lastLen = len;
        for (long i = 0; i < len; i++) data[i] = static_cast<unsigned char>(i);
        return pipeResult >= 0 ? len : pipeResult;
    }
};

int main() {
    FakePort port;
    // presence : bit 2 of WireOut 0x20
    port.wireOut[kMicWireOutStatus] = 0;
    assert(!micPresent(&port));
    port.wireOut[kMicWireOutStatus] = kMicStatusPresent | kMicStatusRun;
    assert(micPresent(&port) && (micStatus(&port) & kMicStatusRun));

    // reset pulse then enable with the sampling delay in bits [15:8]
    micSetEnabled(&port, true, 7);
    assert(port.controlHistory.size() == 2 && port.controlHistory[0] == 0x2 && port.controlHistory[1] == ((7u << 8) | 1u));
    micSetEnabled(&port, false);
    assert(port.controlHistory.back() == 0x0);

    // available(): FIFO level + DDR4 ring {peak, used} from one UpdateWireOuts
    AudioPipeOverClassicDataPort pipe(&port);
    port.wireOut[kMicWireOutLevel] = 32768;
    port.wireOut[kMicWireOutRing] = (838u << 16) | 21u;
    const int before = port.updateOuts;
    assert(pipe.available() == 32768);
    assert(port.updateOuts == before + 1);
    assert(pipe.ringUsed() == 21 && pipe.ringPeak() == 838);

    // read(): pipe 0xA1, 1024-byte blocks, exact length checked
    std::vector<uint8_t> buf(kAudioPacketBytes * 2);
    pipe.read(buf.data(), buf.size());
    assert(port.lastEp == 0xA1 && port.lastBlock == 1024 && port.lastLen == long(buf.size()));
    port.pipeResult = -3;
    bool threw = false;
    try { pipe.read(buf.data(), buf.size()); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);

    // counters
    port.wireOut[kMicWireOutDropped] = 5; port.wireOut[kMicWireOutOverrun] = 1;
    const MicCounters c = micCounters(&port);
    assert(c.dropped == 5 && c.overruns == 1 && c.level == 32768 && c.ringUsed == 21 && c.ringPeak == 838);

    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
