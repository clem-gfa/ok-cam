/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// UserIoDriver.h — the two user LEDs and the two push buttons of the DARWIN board (XEM8310).
//
// Register-access layer for the AXI GPIO added by Gateware/XEM8310-AU25P/IMX219/add_userio.tcl
// (build the gateware with `set with_userio 1`). Base 0x40000000:
//   GPIO_DATA  (0x00): bit0 = LED_0, bit1 = LED_1 (1 = lit), read/write
//   GPIO2_DATA (0x08): bit0 = User_Button, bit1 = Reset_Button. The buttons are active low on the board
//                      (pull-up, pressed = 0); this driver reports "pressed" = true.
// Presence can be probed with isPresent(): a bitfile without the GPIO answers DECERR (AxiError).

#pragma once

#include <cstdint>

#include "Axi.h"

namespace okcli {

// What the UI needs from the user I/O: implemented by the real driver and by SimulatedUserIo.
class IUserIo {
public:
    virtual ~IUserIo() = default;
    virtual uint32_t leds() = 0;
    virtual void setLed(uint32_t led, bool on) = 0;
    virtual uint32_t buttonsPressed() = 0;
};

// Software stand-in (no hardware access) to try the UI with a bitfile that has no user I/O block.
class SimulatedUserIo : public IUserIo {
public:
    uint32_t leds() override { return m_leds; }
    void setLed(uint32_t led, bool on) override { m_leds = on ? (m_leds | (led & 3u)) : (m_leds & ~led); }
    uint32_t buttonsPressed() override { return m_buttons; }
    // Simulates pressing / releasing a button (UserIoDriver::BUTTON_USER or BUTTON_RESET).
    void press(uint32_t button, bool down) { m_buttons = down ? (m_buttons | (button & 3u)) : (m_buttons & ~button); }

private:
    uint32_t m_leds = 0, m_buttons = 0;
};

class UserIoDriver : public IUserIo {
public:
    static constexpr uint32_t LED_0         = 1u << 0;
    static constexpr uint32_t LED_1         = 1u << 1;
    static constexpr uint32_t BUTTON_USER   = 1u << 0;
    static constexpr uint32_t BUTTON_RESET  = 1u << 1;

    explicit UserIoDriver(IAxiLite& axiLite) : m_axi(axiLite) {}

    // True if the loaded bitfile contains the user I/O block.
    bool isPresent() {
        try {
            (void)m_axi.read32(BASE + GPIO2_DATA);
            return true;
        } catch (const AxiError&) {
            return false;
        }
    }

    // LED bitmask (LED_0 | LED_1): bits set are lit.
    uint32_t leds() override { return m_axi.read32(BASE + GPIO_DATA) & LED_MASK; }
    void setLeds(uint32_t mask) { m_axi.write32(BASE + GPIO_DATA, mask & LED_MASK); }
    void setLed(uint32_t led, bool on) override {
        const uint32_t v = leds();
        setLeds(on ? (v | led) : (v & ~led));
    }

    // Bitmask of the buttons currently pressed (BUTTON_USER | BUTTON_RESET).
    uint32_t buttonsPressed() override { return (~m_axi.read32(BASE + GPIO2_DATA)) & BUTTON_MASK; }
    bool userButtonPressed() { return (buttonsPressed() & BUTTON_USER) != 0; }
    bool resetButtonPressed() { return (buttonsPressed() & BUTTON_RESET) != 0; }

private:
    static constexpr uint64_t BASE       = 0x40000000ull;
    static constexpr uint64_t GPIO_DATA  = 0x00;
    static constexpr uint64_t GPIO2_DATA = 0x08;
    static constexpr uint32_t LED_MASK    = LED_0 | LED_1;
    static constexpr uint32_t BUTTON_MASK = BUTTON_USER | BUTTON_RESET;

    IAxiLite& m_axi;
};

}  // namespace okcli
