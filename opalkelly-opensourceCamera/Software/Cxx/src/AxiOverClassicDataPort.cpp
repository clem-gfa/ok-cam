/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AxiOverClassicDataPort.cpp — see header.

#include "AxiOverClassicDataPort.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace okcli {
namespace {

// ---- FrontPanel-to-AXI-Lite bridge endpoint addresses (reference defaults) ----------------
constexpr int BRIDGE_WI_ADDRESS = 0x1d;  // WireIn: AXI byte address
constexpr int BRIDGE_WI_DATA    = 0x1e;  // WireIn: write data
constexpr int BRIDGE_WI_TIMEOUT = 0x1f;  // WireIn: hardware timeout (FP clock periods)
constexpr int BRIDGE_WO_DATA    = 0x3e;  // WireOut: read data
constexpr int BRIDGE_WO_STATUS  = 0x3f;  // WireOut: status
constexpr int BRIDGE_TI_OPERATION = 0x5f;  // TriggerIn: kick a transaction
constexpr int BRIDGE_TI_WRITE_BIT = 0;
constexpr int BRIDGE_TI_READ_BIT  = 1;
constexpr int AXI_RESET_WI = 0x00;  // WireIn: AXI system reset (axi_reset module)

// ---- Status register decode ----------------------------------------------------------------
constexpr uint32_t STATUS_BUSY_MASK     = 0x01;
constexpr int      STATUS_RESPONSE_SHIFT = 1;
constexpr uint32_t STATUS_RESPONSE_MASK  = 0x07;  // 3 bits

constexpr uint32_t RESPONSE_OKAY       = 0b000;
constexpr uint32_t RESPONSE_SLVERR     = 0b010;
constexpr uint32_t RESPONSE_DECERR     = 0b011;
constexpr uint32_t RESPONSE_HW_TIMEOUT = 0b100;

// ---- Timing --------------------------------------------------------------------------------
constexpr double NS_PER_FRONTPANEL_CLOCK_PERIOD = 9.920;
constexpr double MS_TO_NS = 1e6;
constexpr int    STATUS_CHECK_INTERVAL_MS = 10;
constexpr int    DEFAULT_HARDWARE_TIMEOUT_MS = 3000;
constexpr int    DEFAULT_SOFTWARE_TIMEOUT_MS = 5000;

// ---- BlockPipeOut stream -------------------------------------------------------------------
constexpr int STREAM_PIPE_ADDRESS = 0xa0;
constexpr int STREAM_BLOCK_SIZE   = 1024;

inline int ec(OpalKelly::ErrorCode e) { return static_cast<int>(e); }  // NoError == 0

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

}  // namespace

AxiLiteOverClassicDataPort::AxiLiteOverClassicDataPort(OpalKelly::FPGADataPortClassic* dataPort)
    : m_dataPort(dataPort) {
    // Configure the bridge's hardware timeout (in FrontPanel clock periods). We defer
    // UpdateWireIns to the first read/write — it flushes this WireIn along with it.
    const uint32_t timeoutClockPeriods = static_cast<uint32_t>(
        (DEFAULT_HARDWARE_TIMEOUT_MS * MS_TO_NS) / NS_PER_FRONTPANEL_CLOCK_PERIOD);
    m_dataPort->SetWireInValue(BRIDGE_WI_TIMEOUT, timeoutClockPeriods, 0xFFFFFFFF);
}

uint32_t AxiLiteOverClassicDataPort::read32(uint64_t address) {
    const uint32_t addr32 = static_cast<uint32_t>(address);

    // Set address and trigger a read.
    m_dataPort->SetWireInValue(BRIDGE_WI_ADDRESS, addr32, 0xFFFFFFFF);
    m_dataPort->UpdateWireIns();
    m_dataPort->ActivateTriggerIn(BRIDGE_TI_OPERATION, BRIDGE_TI_READ_BIT);

    pollUntilReady("read", addr32);

    m_dataPort->UpdateWireOuts();
    return static_cast<uint32_t>(m_dataPort->GetWireOutValue(BRIDGE_WO_DATA));
}

void AxiLiteOverClassicDataPort::write32(uint64_t address, uint32_t value) {
    const uint32_t addr32 = static_cast<uint32_t>(address);

    // Set address and data, then trigger a write.
    m_dataPort->SetWireInValue(BRIDGE_WI_ADDRESS, addr32, 0xFFFFFFFF);
    m_dataPort->SetWireInValue(BRIDGE_WI_DATA, value, 0xFFFFFFFF);
    m_dataPort->UpdateWireIns();
    m_dataPort->ActivateTriggerIn(BRIDGE_TI_OPERATION, BRIDGE_TI_WRITE_BIT);

    pollUntilReady("write", addr32);
}

void AxiLiteOverClassicDataPort::resetSystem() {
    // Pulse the AXI system reset: assert then deassert axis_aresetn.
    m_dataPort->SetWireInValue(AXI_RESET_WI, 1, 0xFFFFFFFF);
    m_dataPort->UpdateWireIns();
    m_dataPort->SetWireInValue(AXI_RESET_WI, 0, 0xFFFFFFFF);
    m_dataPort->UpdateWireIns();
}

void AxiLiteOverClassicDataPort::pollUntilReady(const char* operation, uint32_t address) {
    const auto startTime = std::chrono::steady_clock::now();

    m_dataPort->UpdateWireOuts();
    uint32_t rawStatus = static_cast<uint32_t>(m_dataPort->GetWireOutValue(BRIDGE_WO_STATUS));

    while ((rawStatus & STATUS_BUSY_MASK) != 0) {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - startTime)
                                   .count();
        if (elapsedMs > DEFAULT_SOFTWARE_TIMEOUT_MS) {
            throw AxiError(std::string("AXI-Lite bridge ") + operation + " timed out at address " +
                           hex(address) + " after " + std::to_string(elapsedMs) + "ms");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(STATUS_CHECK_INTERVAL_MS));
        m_dataPort->UpdateWireOuts();
        rawStatus = static_cast<uint32_t>(m_dataPort->GetWireOutValue(BRIDGE_WO_STATUS));
    }

    const uint32_t responseBits = (rawStatus >> STATUS_RESPONSE_SHIFT) & STATUS_RESPONSE_MASK;
    switch (responseBits) {
        case RESPONSE_OKAY:
            return;
        case RESPONSE_SLVERR:
            throw AxiError(std::string("AXI-Lite ") + operation + " SLVERR at address " + hex(address));
        case RESPONSE_DECERR:
            throw AxiError(std::string("AXI-Lite ") + operation + " DECERR (no slave) at address " +
                           hex(address));
        case RESPONSE_HW_TIMEOUT:
            throw AxiError(std::string("AXI-Lite ") + operation + " hardware timeout at address " +
                           hex(address) + ". Reset AXI system to recover.");
        default:
            throw AxiError(std::string("AXI-Lite ") + operation + " unknown response at address " +
                           hex(address));
    }
}

AxiStreamOverClassicDataPort::AxiStreamOverClassicDataPort(OpalKelly::FPGADataPortClassic* dataPort)
    : m_dataPort(dataPort) {}

void AxiStreamOverClassicDataPort::read(uint8_t* buffer, std::size_t size, uint32_t /*timeoutMs*/) {
    const long got = m_dataPort->ReadFromBlockPipeOut(
        STREAM_PIPE_ADDRESS, STREAM_BLOCK_SIZE, static_cast<long>(size), buffer);
    if (got < 0) {
        throw AxiError("AXI-Stream BlockPipeOut read failed (code " + std::to_string(got) + ")");
    }
    if (static_cast<std::size_t>(got) != size) {
        throw AxiError("AXI-Stream short read: got " + std::to_string(got) + " of " +
                       std::to_string(size) + " bytes");
    }
}

}  // namespace okcli
