// Test hors materiel de UserIoDriver.h avec un bus AXI-Lite simule.
// Build : g++ -std=c++17 -I../src user_io_test.cpp -o user_io_test
#include <cassert>
#include <iostream>
#include <map>

#include "UserIoDriver.h"

using namespace okcli;

class FakeAxi : public IAxiLite {
public:
    bool present = true;
    std::map<uint64_t, uint32_t> regs;
    uint32_t read32(uint64_t a) override {
        if (!present || a < 0x40000000 || a >= 0x40010000) throw AxiError("AXI-Lite read DECERR (no slave)");
        return regs[a];
    }
    void write32(uint64_t a, uint32_t v) override {
        if (!present || a < 0x40000000 || a >= 0x40010000) throw AxiError("AXI-Lite write DECERR (no slave)");
        regs[a] = v;
    }
    void resetSystem() override {}
};

int main() {
    FakeAxi bus;
    UserIoDriver io(bus);
    assert(io.isPresent());

    // LED : lecture-modification-ecriture, masque a 2 bits
    assert(io.leds() == 0);
    io.setLed(UserIoDriver::LED_0, true);
    assert(bus.regs[0x40000000] == 1 && io.leds() == 1);
    io.setLed(UserIoDriver::LED_1, true);
    assert(bus.regs[0x40000000] == 3);
    io.setLed(UserIoDriver::LED_0, false);
    assert(bus.regs[0x40000000] == 2);
    io.setLeds(0xFFFFFFFF);
    assert(bus.regs[0x40000000] == 3);                    // les autres bits sont ignores

    // Boutons : actifs a l'etat bas sur la carte, « appuye » = true
    bus.regs[0x40000008] = 0x3;                          // repos : les deux a 1
    assert(io.buttonsPressed() == 0 && !io.userButtonPressed() && !io.resetButtonPressed());
    bus.regs[0x40000008] = 0x2;                          // User appuye (bit0 = 0)
    assert(io.userButtonPressed() && !io.resetButtonPressed() && io.buttonsPressed() == UserIoDriver::BUTTON_USER);
    bus.regs[0x40000008] = 0x1;                          // Reset appuye
    assert(!io.userButtonPressed() && io.resetButtonPressed());
    bus.regs[0x40000008] = 0xFFFFFFF0 | 0x3;             // bits hauts ignores
    assert(io.buttonsPressed() == 0);

    // bitfile sans le bloc : DECERR -> non present
    bus.present = false;
    assert(!io.isPresent());

    // simulated backend (used by the UI when the bitfile has no user I/O block)
    SimulatedUserIo sim;
    assert(sim.leds() == 0);
    sim.setLed(UserIoDriver::LED_1, true);
    assert(sim.leds() == UserIoDriver::LED_1);
    sim.setLed(UserIoDriver::LED_1, false);
    assert(sim.leds() == 0);
    sim.press(UserIoDriver::BUTTON_USER, true);
    assert(sim.buttonsPressed() == UserIoDriver::BUTTON_USER);
    sim.press(UserIoDriver::BUTTON_RESET, true);
    sim.press(UserIoDriver::BUTTON_USER, false);
    assert(sim.buttonsPressed() == UserIoDriver::BUTTON_RESET);

    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
