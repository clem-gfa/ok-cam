/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// I2CController.h — AMD AXI IIC (dynamic mode) over IAxiLite.
//
// Shared by both transports (the AXI-Lite seam is transport-agnostic). Supports 16-bit register
// / 16-bit data (AR0330) and 16-bit register / 8-bit data (OV5640) accesses.

#pragma once

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "Axi.h"

namespace okcli {

class I2CController {
public:
    explicit I2CController(IAxiLite& axiLite) : m_axi(axiLite) {}

    // Initialize the AXI IIC IP in dynamic mode. Idempotent (begins with a soft reset).
    void initialize() {
        m_axi.write32(IIC_BASE + XIIC_RESETR_OFFSET, XIIC_RESET_MASK);
        m_axi.write32(IIC_BASE + XIIC_RFD_REG_OFFSET, IIC_RX_FIFO_DEPTH - 1);
        m_axi.write32(IIC_BASE + XIIC_CR_REG_OFFSET, XIIC_CR_TX_FIFO_RESET_MASK);
        m_axi.write32(IIC_BASE + XIIC_CR_REG_OFFSET, XIIC_CR_ENABLE_DEVICE_MASK);

        const uint32_t st = m_axi.read32(IIC_BASE + XIIC_SR_REG_OFFSET);
        const uint32_t expected = XIIC_SR_RX_FIFO_EMPTY_MASK | XIIC_SR_TX_FIFO_EMPTY_MASK;
        if ((st & expected) != expected) {
            throw AxiError("AXI IIC dynamic init failed. Status=0x" + toHex(st));
        }
    }

    // 16-bit register address, 16-bit data (AR0330).
    uint16_t read16(int deviceAddress, uint16_t registerAddress) {
        if (!waitBusFree()) throw AxiError("I2C bus never became free before read16()");
        dynSend(deviceAddress, {byteHi(registerAddress), byteLo(registerAddress)}, false);
        const std::vector<uint8_t> rx = dynRecv(deviceAddress, 2);
        if (!waitBusFree()) throw AxiError("I2C bus did not free after read16()");
        return static_cast<uint16_t>((rx[0] << 8) | rx[1]);
    }

    void write16(int deviceAddress, uint16_t registerAddress, uint16_t data) {
        if (!waitBusFree()) throw AxiError("I2C bus never became free before write16()");
        dynSend(deviceAddress,
                {byteHi(registerAddress), byteLo(registerAddress), byteHi(data), byteLo(data)}, true);
    }

    // 16-bit register address, 8-bit data (OV5640 / PCAM).
    uint8_t read8(int dev7bit, uint16_t registerAddress) {
        if (!waitBusFree()) throw AxiError("I2C bus never became free before read8()");
        dynSend(dev7bit, {byteHi(registerAddress), byteLo(registerAddress)}, false);
        const std::vector<uint8_t> rx = dynRecv(dev7bit, 1);
        if (!waitBusFree()) throw AxiError("I2C bus did not free after read8()");
        return rx[0];
    }

    void write8(int dev7bit, uint16_t registerAddress, uint8_t data) {
        if (!waitBusFree()) throw AxiError("I2C bus never became free before write8()");
        dynSend(dev7bit, {byteHi(registerAddress), byteLo(registerAddress), data}, true);
    }

private:
    // --- AXI IIC register offsets / masks (from xiic) -------------------------------------
    static constexpr uint64_t IIC_BASE             = 0x40800000ull;
    static constexpr uint64_t XIIC_RESETR_OFFSET   = 0x40;
    static constexpr uint64_t XIIC_CR_REG_OFFSET   = 0x100;
    static constexpr uint64_t XIIC_SR_REG_OFFSET   = 0x104;
    static constexpr uint64_t XIIC_DTR_REG_OFFSET  = 0x108;
    static constexpr uint64_t XIIC_DRR_REG_OFFSET  = 0x10c;
    static constexpr uint64_t XIIC_RFD_REG_OFFSET  = 0x120;

    static constexpr uint32_t XIIC_RESET_MASK            = 0x0a;
    static constexpr uint32_t XIIC_CR_ENABLE_DEVICE_MASK = 0x01;
    static constexpr uint32_t XIIC_CR_TX_FIFO_RESET_MASK = 0x02;
    static constexpr uint32_t XIIC_SR_BUS_BUSY_MASK      = 0x04;
    static constexpr uint32_t XIIC_SR_RX_FIFO_EMPTY_MASK = 0x40;
    static constexpr uint32_t XIIC_SR_TX_FIFO_EMPTY_MASK = 0x80;
    static constexpr uint32_t XIIC_TX_DYN_START_MASK     = 0x100;
    static constexpr uint32_t XIIC_TX_DYN_STOP_MASK      = 0x200;
    static constexpr int      IIC_RX_FIFO_DEPTH          = 16;

    static uint8_t byteHi(uint16_t v) { return static_cast<uint8_t>((v >> 8) & 0xff); }
    static uint8_t byteLo(uint16_t v) { return static_cast<uint8_t>(v & 0xff); }

    static std::string toHex(uint32_t v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%x", v);
        return b;
    }

    bool waitBusFree(int timeoutMs = 1000) {
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            const uint32_t sr = m_axi.read32(IIC_BASE + XIIC_SR_REG_OFFSET);
            if ((sr & XIIC_SR_BUS_BUSY_MASK) == 0) return true;
            if (elapsedMs(start) > timeoutMs) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void dynSend(int dev7bit, const std::vector<uint8_t>& bytes, bool sendStop) {
        const uint32_t addrByte = static_cast<uint32_t>((dev7bit << 1) | 0);
        m_axi.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_START_MASK | addrByte);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (i == bytes.size() - 1 && sendStop) {
                m_axi.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_STOP_MASK | bytes[i]);
            } else {
                m_axi.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, bytes[i]);
            }
        }
    }

    std::vector<uint8_t> dynRecv(int dev7bit, int count, int timeoutMs = 1000) {
        if (count <= 0 || count > 255) throw AxiError("I2C recv byte count must be 1..255");

        const uint32_t addrByte = static_cast<uint32_t>((dev7bit << 1) | 1);
        m_axi.write32(IIC_BASE + XIIC_DTR_REG_OFFSET, XIIC_TX_DYN_START_MASK | addrByte);
        m_axi.write32(IIC_BASE + XIIC_DTR_REG_OFFSET,
                      XIIC_TX_DYN_STOP_MASK | static_cast<uint32_t>(count));

        std::vector<uint8_t> result;
        const auto start = std::chrono::steady_clock::now();
        while (static_cast<int>(result.size()) < count) {
            if (elapsedMs(start) > timeoutMs) {
                throw AxiError("I2C recv timed out (received " + std::to_string(result.size()) +
                               "/" + std::to_string(count) + " bytes)");
            }
            const uint32_t sr = m_axi.read32(IIC_BASE + XIIC_SR_REG_OFFSET);
            if ((sr & XIIC_SR_RX_FIFO_EMPTY_MASK) == 0) {
                const uint32_t b = m_axi.read32(IIC_BASE + XIIC_DRR_REG_OFFSET);
                result.push_back(static_cast<uint8_t>(b & 0xff));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        return result;
    }

    static long elapsedMs(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start)
            .count();
    }

    IAxiLite& m_axi;
};

}  // namespace okcli
