/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AxiOverClassicDataPort.h — AXI over the Classic FrontPanel data port (XEM8320).
//
// Translates 32-bit AXI-Lite register
// reads/writes into WireIn/WireOut/TriggerIn transactions through the FrontPanelToAxiLiteBridge
// gateware, and AXI-Stream reads into a BlockPipeOut transfer.

#pragma once

#include <functional>

#include "Axi.h"
#include "okFrontPanel.h"

namespace okcli {

// AXI-Lite backed by the Classic FrontPanel endpoints via the FrontPanelToAxiLiteBridge.
// Drives AXI-Lite over the FrontPanel classic data port (FrontPanel-to-AXI bridge).
class AxiLiteOverClassicDataPort : public IAxiLite {
public:
    explicit AxiLiteOverClassicDataPort(OpalKelly::FPGADataPortClassic* dataPort);

    uint32_t read32(uint64_t address) override;
    void write32(uint64_t address, uint32_t value) override;
    void resetSystem() override;

private:
    // Poll the bridge STATUS wire until BUSY clears, then map the AXI response to OK/throw.
    void pollUntilReady(const char* operation, uint32_t address);

    OpalKelly::FPGADataPortClassic* m_dataPort;  // owned by the okCFrontPanel, not by us
};

// AXI-Stream backed by a Classic BlockPipeOut endpoint (the Pipe-to-AXI-Stream shim).
class AxiStreamOverClassicDataPort : public IAxiStream {
public:
    explicit AxiStreamOverClassicDataPort(OpalKelly::FPGADataPortClassic* dataPort);

    // NOTE: the underlying BlockPipeOut transfer has no timeout and is not cancellable; on a
    // stall recovery requires AxiLite::resetSystem(). `timeoutMs` is
    // accepted for interface parity but a too-short transfer simply throws afterward.
    void read(uint8_t* buffer, std::size_t size, uint32_t timeoutMs) override;

    // Registers a function called between slices of every large transfer. A video frame is read in ONE transfer that
    // keeps the USB pipe busy for tens of ms; the microphone gateware only buffers a few ms of audio in front of its
    // DDR4 ring, so the audio is serviced between slices of kStreamSliceBytes. The hook runs on the calling thread and
    // must not throw. Pass an empty function to read in one piece.
    void setBetweenChunksHook(std::function<void()> hook) { m_betweenChunks = std::move(hook); }

private:
    void readSlice(uint8_t* buffer, std::size_t size);

    OpalKelly::FPGADataPortClassic* m_dataPort;
    std::function<void()> m_betweenChunks;
};

}  // namespace okcli
