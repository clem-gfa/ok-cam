// Hardware-free test of src/UserIoDriver.ts. Run: npm run test:userio
import * as assert from "assert";

import { IAxiLite } from "../src/IAxi";
import { BUTTON_RESET, BUTTON_USER, LED_0, LED_1, SimulatedUserIo, UserIoDriver } from "../src/UserIoDriver";

class FakeAxi implements IAxiLite {
    present = true;
    regs = new Map<number, number>();
    private check(a: number, op: string) {
        if (!this.present || a < 0x40000000 || a >= 0x40010000) throw new Error(`AXI-Lite ${op} DECERR (no slave) at address 0x${a.toString(16)}`);
    }
    async read32(a: number) {
        this.check(a, "read");
        return this.regs.get(a) ?? 0;
    }
    async write32(a: number, v: number) {
        this.check(a, "write");
        this.regs.set(a, v >>> 0);
    }
    async resetSystem() {}
}

async function main() {
    const bus = new FakeAxi();
    const io = new UserIoDriver(bus);
    assert.strictEqual(await io.isPresent(), true);

    assert.strictEqual(await io.leds(), 0);
    await io.setLed(LED_0, true);
    assert.strictEqual(bus.regs.get(0x40000000), 1);
    await io.setLed(LED_1, true);
    assert.strictEqual(bus.regs.get(0x40000000), 3);
    await io.setLed(LED_0, false);
    assert.strictEqual(bus.regs.get(0x40000000), 2);
    await io.setLeds(0xffffffff);
    assert.strictEqual(bus.regs.get(0x40000000), 3);

    bus.regs.set(0x40000008, 0x3); // both released (pull-up)
    assert.strictEqual(await io.buttonsPressed(), 0);
    bus.regs.set(0x40000008, 0x2); // User pressed
    assert.strictEqual(await io.buttonsPressed(), BUTTON_USER);
    bus.regs.set(0x40000008, 0x1); // Reset pressed
    assert.strictEqual(await io.buttonsPressed(), BUTTON_RESET);
    bus.regs.set(0x40000008, 0x0);
    assert.strictEqual(await io.buttonsPressed(), BUTTON_USER | BUTTON_RESET);
    bus.regs.set(0x40000008, 0xfffffff3); // upper bits ignored
    assert.strictEqual(await io.buttonsPressed(), 0);

    bus.present = false; // bitfile without the block
    assert.strictEqual(await io.isPresent(), false);

    // other errors are not mistaken for "absent"
    const broken: IAxiLite = { ...bus, read32: async () => { throw new Error("AXI-Lite read hardware timeout"); }, write32: async () => {}, resetSystem: async () => {} };
    await assert.rejects(() => new UserIoDriver(broken).isPresent());

    // simulated backend (used by the UI when the bitfile has no user I/O block)
    const sim = new SimulatedUserIo();
    assert.strictEqual(await sim.leds(), 0);
    await sim.setLed(LED_1, true);
    assert.strictEqual(await sim.leds(), LED_1);
    await sim.setLed(LED_1, false);
    assert.strictEqual(await sim.leds(), 0);
    sim.press(BUTTON_USER, true);
    assert.strictEqual(await sim.buttonsPressed(), BUTTON_USER);
    sim.press(BUTTON_RESET, true);
    sim.press(BUTTON_USER, false);
    assert.strictEqual(await sim.buttonsPressed(), BUTTON_RESET);

    console.log("ALL USER IO TESTS PASSED");
}

main().catch((e) => {
    console.error(e);
    process.exit(1);
});
