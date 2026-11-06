/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// VideoDMADriver.h — AMD AXI VDMA driver.
//
// Stateless register-access layer for the AXI VDMA IP (base 0x44A00000). Manages the S2MM
// (write) and MM2S (read) channels with triple-buffer support. Writing VSIZE last starts a
// channel.

#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#include "Axi.h"

namespace okcli {

class VideoDMADriver {
public:
    explicit VideoDMADriver(IAxiLite& axiLite) : m_axi(axiLite) {}

    // Stop the S2MM (write) channel and poll until halted. Returns false on timeout.
    bool stopWriteChannel(int timeoutMs = 1000) {
        const uint32_t cr = m_axi.read32(VDMA_BASE + VDMA_S2MM_VDMACR);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_VDMACR, cr & ~VDMA_CR_RUNSTOP_MASK);
        return pollHalted(VDMA_BASE + VDMA_S2MM_VDMASR, timeoutMs);
    }

    // Stop the MM2S (read) channel and poll until halted. Returns false on timeout.
    bool stopReadChannel(int timeoutMs = 1000) {
        const uint32_t cr = m_axi.read32(VDMA_BASE + VDMA_MM2S_VDMACR);
        m_axi.write32(VDMA_BASE + VDMA_MM2S_VDMACR, cr & ~VDMA_CR_RUNSTOP_MASK);
        return pollHalted(VDMA_BASE + VDMA_MM2S_VDMASR, timeoutMs);
    }

    // Soft reset both channels.
    void softReset() {
        m_axi.write32(VDMA_BASE + VDMA_MM2S_VDMACR, VDMA_CR_RESET_MASK);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_VDMACR, VDMA_CR_RESET_MASK);
    }

    // Configure + start the S2MM (write) channel in triple-buffer mode (VSIZE last = start).
    void startWriteChannel(uint32_t widthBytes, uint32_t height, uint32_t buf0, uint32_t buf1,
                           uint32_t buf2) {
        m_axi.write32(VDMA_BASE + VDMA_S2MM_VDMACR, 0x8b);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_HSIZE, widthBytes);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_FRMDLY_STRIDE, widthBytes);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_START_ADDR1, buf0);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_START_ADDR2, buf1);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_START_ADDR3, buf2);
        m_axi.write32(VDMA_BASE + VDMA_S2MM_VSIZE, height);  // writing VSIZE starts S2MM
    }

    // Configure + start the MM2S (read) channel in triple-buffer mode (VSIZE last = start).
    void startReadChannel(uint32_t widthBytes, uint32_t height, uint32_t buf0, uint32_t buf1,
                          uint32_t buf2) {
        m_axi.write32(VDMA_BASE + VDMA_MM2S_VDMACR, 0x8b);
        m_axi.write32(VDMA_BASE + VDMA_MM2S_HSIZE, widthBytes);
        m_axi.write32(VDMA_BASE + VDMA_MM2S_FRMDLY_STRIDE, widthBytes);
        m_axi.write32(VDMA_BASE + VDMA_MM2S_START_ADDR1, buf0);
        m_axi.write32(VDMA_BASE + VDMA_MM2S_START_ADDR2, buf1);
        m_axi.write32(VDMA_BASE + VDMA_MM2S_START_ADDR3, buf2);

        // Park on frame 1 for circular buffering.
        uint32_t parkptr = m_axi.read32(VDMA_BASE + VDMA_PARKPTR);
        parkptr = (parkptr & ~0xfu) | 0x1u;
        m_axi.write32(VDMA_BASE + VDMA_PARKPTR, parkptr);

        m_axi.write32(VDMA_BASE + VDMA_MM2S_VSIZE, height);  // writing VSIZE starts MM2S
    }

    void clearStatus() {
        m_axi.write32(VDMA_BASE + VDMA_S2MM_VDMASR, 0xffffffffu);
        m_axi.write32(VDMA_BASE + VDMA_MM2S_VDMASR, 0xffffffffu);
    }

    uint32_t getWriteChannelStatus() { return m_axi.read32(VDMA_BASE + VDMA_S2MM_VDMASR); }
    uint32_t getReadChannelStatus() { return m_axi.read32(VDMA_BASE + VDMA_MM2S_VDMASR); }

private:
    static constexpr uint64_t VDMA_BASE = 0x44a00000ull;
    // MM2S (read) channel
    static constexpr uint64_t VDMA_MM2S_VDMACR = 0x00;
    static constexpr uint64_t VDMA_MM2S_VDMASR = 0x04;
    static constexpr uint64_t VDMA_PARKPTR     = 0x28;
    // S2MM (write) channel
    static constexpr uint64_t VDMA_S2MM_VDMACR = 0x30;
    static constexpr uint64_t VDMA_S2MM_VDMASR = 0x34;
    // MM2S size/stride/addr
    static constexpr uint64_t VDMA_MM2S_VSIZE        = 0x50;
    static constexpr uint64_t VDMA_MM2S_HSIZE        = 0x54;
    static constexpr uint64_t VDMA_MM2S_FRMDLY_STRIDE = 0x58;
    static constexpr uint64_t VDMA_MM2S_START_ADDR1  = 0x5c;
    static constexpr uint64_t VDMA_MM2S_START_ADDR2  = 0x60;
    static constexpr uint64_t VDMA_MM2S_START_ADDR3  = 0x64;
    // S2MM size/stride/addr
    static constexpr uint64_t VDMA_S2MM_VSIZE        = 0xa0;
    static constexpr uint64_t VDMA_S2MM_HSIZE        = 0xa4;
    static constexpr uint64_t VDMA_S2MM_FRMDLY_STRIDE = 0xa8;
    static constexpr uint64_t VDMA_S2MM_START_ADDR1  = 0xac;
    static constexpr uint64_t VDMA_S2MM_START_ADDR2  = 0xb0;
    static constexpr uint64_t VDMA_S2MM_START_ADDR3  = 0xb4;
    // Control bits
    static constexpr uint32_t VDMA_CR_RUNSTOP_MASK = 0x01;
    static constexpr uint32_t VDMA_CR_RESET_MASK   = 0x04;

    bool pollHalted(uint64_t srAddr, int timeoutMs) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start)
                   .count() < timeoutMs) {
            if (m_axi.read32(srAddr) & 0x1u) return true;  // halted
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    IAxiLite& m_axi;
};

}  // namespace okcli
