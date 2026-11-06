/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Axi.h — the transport seam between the two device types.
//
// Everything above the transport (I2C, sub-drivers, camera controls, the capture sequencer) talks
// to the hardware exclusively through these two interfaces, so the only thing that differs between
// the XEM8320 (classic) and the SZG-HUB1450 (AXI) is which concrete transport is constructed.
//
// Errors are reported by throwing AxiError.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace okcli {

// Thrown by the transport (and layers above it) on any bus error or timeout.
class AxiError : public std::runtime_error {
public:
    explicit AxiError(const std::string& message) : std::runtime_error(message) {}
};

// AXI-Lite register access. The IAxiLite interface.
class IAxiLite {
public:
    virtual ~IAxiLite() = default;

    // Read a 32-bit register at the given byte address.
    virtual uint32_t read32(uint64_t address) = 0;

    // Write a 32-bit value to the register at the given byte address.
    virtual void write32(uint64_t address, uint32_t value) = 0;

    // Assert the AXI system reset line (axis_aresetn).
    //   * native AXI path  → axiStream.Reset()
    //   * classic path     → toggles WireIn 0x00
    // WARNING: invalidates I2C state — callers must re-initialize the I2C controller after.
    virtual void resetSystem() = 0;
};

// AXI-Stream bulk read (read-only). The IAxiStream interface.
class IAxiStream {
public:
    virtual ~IAxiStream() = default;

    // Read exactly `size` bytes into `buffer`. Throws AxiError on timeout/short read.
    virtual void read(uint8_t* buffer, std::size_t size, uint32_t timeoutMs) = 0;
};

}  // namespace okcli
