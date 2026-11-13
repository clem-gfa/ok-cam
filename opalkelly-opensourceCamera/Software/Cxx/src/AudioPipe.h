/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AudioPipe.h — IAudioPipe over the Classic block pipe 0xA1 of the XEM8310 microphone gateware,
// plus the WireIn/WireOut helpers to start and monitor the microphones.
//
// NOTE: like the video pipe, a BlockPipeOut transfer has no timeout and cannot be cancelled: keep the
// microphones enabled until AudioCapture::stop() has returned, otherwise the reading thread waits
// for data that never comes.

#pragma once

#include <string>

#include "AudioStream.h"
#include "okFrontPanel.h"

namespace okcli {

constexpr int kMicWireInControl  = 0x01;   // bit0 enable, bit1 reset, bits[15:8] sampling delay
constexpr int kMicWireOutStatus  = 0x20;   // bit0 run, bit1 ultrasonic clock, bit2 PLL locked, bit3 FIFO full seen
constexpr int kMicWireOutDropped = 0x21;
constexpr int kMicWireOutOverrun = 0x22;

class AudioPipeOverClassicDataPort : public IAudioPipe {
public:
    explicit AudioPipeOverClassicDataPort(OpalKelly::FPGADataPortClassic* dataPort) : m_dataPort(dataPort) {}

    void read(uint8_t* buffer, std::size_t size) override {
        const long got = m_dataPort->ReadFromBlockPipeOut(kAudioPipeAddress, kAudioBlockSize, static_cast<long>(size), buffer);
        if (got < 0) throw std::runtime_error("audio BlockPipeOut read failed (code " + std::to_string(got) + ")");
        if (static_cast<std::size_t>(got) != size)
            throw std::runtime_error("audio short read: " + std::to_string(got) + " of " + std::to_string(size) + " bytes");
    }

private:
    OpalKelly::FPGADataPortClassic* m_dataPort;
};

}  // namespace okcli
