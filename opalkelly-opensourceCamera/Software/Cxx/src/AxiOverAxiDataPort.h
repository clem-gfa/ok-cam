/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AxiOverAxiDataPort.h — AXI over the native AXI data port (SZG-HUB1450).
//
// A thin pass-through to the FrontPanel-Platform
// native AXI-Lite / AXI-Stream data port.

#pragma once

#include "Axi.h"
#include "okFrontPanel.h"

namespace okcli {

// AXI-Lite backed by the native AXI data port.
class AxiLiteOverAxiDataPort : public IAxiLite {
public:
    explicit AxiLiteOverAxiDataPort(OpalKelly::FPGADataPortAXI* port);

    uint32_t read32(uint64_t address) override;
    void write32(uint64_t address, uint32_t value) override;
    void resetSystem() override;

private:
    // okCFrontPanelAXILite/AXIStream are lightweight value wrappers around a handle owned by
    // the data port; we hold copies for the lifetime of this transport.
    OpalKelly::FrontPanelAXILite  m_axiLite;
    OpalKelly::FrontPanelAXIStream m_axiStream;
};

// AXI-Stream backed by the native AXI data port.
class AxiStreamOverAxiDataPort : public IAxiStream {
public:
    explicit AxiStreamOverAxiDataPort(OpalKelly::FPGADataPortAXI* port);

    void read(uint8_t* buffer, std::size_t size, uint32_t timeoutMs) override;

private:
    OpalKelly::FrontPanelAXIStream m_axiStream;
};

}  // namespace okcli
