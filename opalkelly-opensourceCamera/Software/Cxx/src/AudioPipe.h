/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AudioPipe.h — IAudioPipe over the Classic block pipe 0xA1 of the XEM8310 microphone gateware, plus the
// WireIn/WireOut helpers to start, monitor and read the status of the microphones. Counterpart of
// Software/FP-Platform/src/AudioPipe.ts.
//
// NOTE: like the video pipe, a BlockPipeOut transfer has no timeout and cannot be cancelled: keep the microphones
// enabled until the audio loop has stopped, otherwise it waits for data that never comes. Call these only from the
// thread that owns the device (the GUI's camera worker).

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "AudioStream.h"
#include "okFrontPanel.h"

namespace okcli {

constexpr int kMicWireInControl  = 0x01;   // bit0 enable, bit1 reset, bits[15:8] sampling delay (clk cycles)
constexpr int kMicWireOutStatus  = 0x20;   // bit0 run, bit1 ultrasonic clock, bit2 present/locked, bit3 FIFO/ring was full
constexpr int kMicWireOutDropped = 0x21;   // packets the gateware dropped (the PC did not read fast enough)
constexpr int kMicWireOutOverrun = 0x22;
constexpr int kMicWireOutLevel   = 0x23;   // 32-bit words waiting in the gateware FIFO
constexpr int kMicWireOutRing    = 0x24;   // {peak[31:16], current[15:0]} packets in the DDR4 ring (0 without the ring)

class AudioPipeOverClassicDataPort : public IAudioPipe {
public:
    explicit AudioPipeOverClassicDataPort(OpalKelly::FPGADataPortClassic* dataPort) : m_dataPort(dataPort) {}

    // FIFO level and DDR4 ring occupancy are read in the same USB transaction.
    uint32_t available() override {
        m_dataPort->UpdateWireOuts();
        const uint32_t ring = static_cast<uint32_t>(m_dataPort->GetWireOutValue(kMicWireOutRing));
        m_ringUsed = ring & 0xFFFF;
        m_ringPeak = (ring >> 16) & 0xFFFF;
        return static_cast<uint32_t>(m_dataPort->GetWireOutValue(kMicWireOutLevel));
    }

    void read(uint8_t* buffer, std::size_t size) override {
        const long got = m_dataPort->ReadFromBlockPipeOut(kAudioPipeAddress, kAudioBlockSize, static_cast<long>(size), buffer);
        if (got < 0) throw std::runtime_error("audio BlockPipeOut read failed (code " + std::to_string(got) + ")");
        if (static_cast<std::size_t>(got) != size)
            throw std::runtime_error("audio short read: " + std::to_string(got) + " of " + std::to_string(size) + " bytes");
    }

    uint32_t ringUsed() const override { return m_ringUsed; }
    uint32_t ringPeak() const override { return m_ringPeak; }

private:
    OpalKelly::FPGADataPortClassic* m_dataPort;
    uint32_t m_ringUsed = 0, m_ringPeak = 0;
};

// True when the loaded bitfile contains the microphone subsystem. Without it, reading the audio pipe would wait for data
// that never comes and block the device thread, so this must be checked first.
inline bool micPresent(OpalKelly::FPGADataPortClassic* dp) {
    dp->UpdateWireOuts();
    return (static_cast<uint32_t>(dp->GetWireOutValue(kMicWireOutStatus)) & kMicStatusPresent) != 0;
}

inline uint32_t micStatus(OpalKelly::FPGADataPortClassic* dp) {
    dp->UpdateWireOuts();
    return static_cast<uint32_t>(dp->GetWireOutValue(kMicWireOutStatus));
}

inline MicCounters micCounters(OpalKelly::FPGADataPortClassic* dp) {
    dp->UpdateWireOuts();
    MicCounters c;
    c.status   = static_cast<uint32_t>(dp->GetWireOutValue(kMicWireOutStatus));
    c.dropped  = static_cast<uint32_t>(dp->GetWireOutValue(kMicWireOutDropped));
    c.overruns = static_cast<uint32_t>(dp->GetWireOutValue(kMicWireOutOverrun));
    c.level    = static_cast<uint32_t>(dp->GetWireOutValue(kMicWireOutLevel));
    const uint32_t ring = static_cast<uint32_t>(dp->GetWireOutValue(kMicWireOutRing));
    c.ringUsed = ring & 0xFFFF;
    c.ringPeak = (ring >> 16) & 0xFFFF;
    return c;
}

// Resets the microphone subsystem (clears its FIFOs and DDR4 ring), then enables or disables it.
inline void micSetEnabled(OpalKelly::FPGADataPortClassic* dp, bool enable, unsigned sampleDelay = 7) {
    dp->SetWireInValue(kMicWireInControl, 0x2, 0xFFFFFFFF);   // reset, disabled
    dp->UpdateWireIns();
    dp->SetWireInValue(kMicWireInControl, enable ? (((sampleDelay & 0xFFu) << 8) | 0x1u) : 0x0u, 0xFFFFFFFF);
    dp->UpdateWireIns();
}

}  // namespace okcli
