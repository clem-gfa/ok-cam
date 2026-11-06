/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AxiOverAxiDataPort.cpp — see header.

#include "AxiOverAxiDataPort.h"

#include <string>

namespace okcli {
namespace {

constexpr uint32_t AXI_TIMEOUT_MS = 5000;  // default AXI-Lite operation timeout

inline bool ok(OpalKelly::ErrorCode e) { return e == OpalKelly::ErrorCode::NoError; }

std::string codeStr(OpalKelly::ErrorCode e) { return std::to_string(static_cast<int>(e)); }

}  // namespace

AxiLiteOverAxiDataPort::AxiLiteOverAxiDataPort(OpalKelly::FPGADataPortAXI* port)
    : m_axiLite(port->GetAXILite()), m_axiStream(port->GetAXIStream()) {}

uint32_t AxiLiteOverAxiDataPort::read32(uint64_t address) {
    UINT32 value = 0;
    const OpalKelly::ErrorCode e = m_axiLite.Read(address, value, AXI_TIMEOUT_MS);
    if (!ok(e)) throw AxiError("AXI-Lite read failed at address (code " + codeStr(e) + ")");
    return value;
}

void AxiLiteOverAxiDataPort::write32(uint64_t address, uint32_t value) {
    const OpalKelly::ErrorCode e = m_axiLite.Write(address, value, AXI_TIMEOUT_MS);
    if (!ok(e)) throw AxiError("AXI-Lite write failed at address (code " + codeStr(e) + ")");
}

void AxiLiteOverAxiDataPort::resetSystem() {
    const OpalKelly::ErrorCode e = m_axiStream.Reset();
    if (!ok(e)) throw AxiError("AXI-Stream reset failed (code " + codeStr(e) + ")");
}

AxiStreamOverAxiDataPort::AxiStreamOverAxiDataPort(OpalKelly::FPGADataPortAXI* port)
    : m_axiStream(port->GetAXIStream()) {}

void AxiStreamOverAxiDataPort::read(uint8_t* buffer, std::size_t size, uint32_t timeoutMs) {
    UINT64 transferred = 0;
    const OpalKelly::ErrorCode e = m_axiStream.Read(buffer, size, timeoutMs, transferred);
    if (!ok(e)) throw AxiError("AXI-Stream read failed (code " + codeStr(e) + ")");
    if (transferred != size) {
        throw AxiError("AXI-Stream short read: got " + std::to_string(transferred) + " of " +
                       std::to_string(size) + " bytes");
    }
}

}  // namespace okcli
