/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { IAxiLite } from "./IAxi";

// The two user LEDs and the two push buttons of the DARWIN board (XEM8310). Register-access layer for the
// AXI GPIO added by Gateware/XEM8310-AU25P/IMX219/add_userio.tcl (build the gateware with `set with_userio 1`).
//   GPIO_DATA  (0x00): bit0 = LED_0, bit1 = LED_1 (1 = lit), read/write
//   GPIO2_DATA (0x08): bit0 = User_Button, bit1 = Reset_Button. Active low on the board (pull-up, pressed = 0);
//                      this driver reports "pressed" = true.
const GPIO_BASE = 0x40000000;
const GPIO_DATA = 0x00;
const GPIO2_DATA = 0x08;

export const LED_0 = 1 << 0;
export const LED_1 = 1 << 1;
export const BUTTON_USER = 1 << 0;
export const BUTTON_RESET = 1 << 1;

const LED_MASK = LED_0 | LED_1;
const BUTTON_MASK = BUTTON_USER | BUTTON_RESET;

/** What the UI needs from the user I/O: implemented by the real driver and by SimulatedUserIo. */
export interface IUserIo {
    leds(): Promise<number>;
    setLed(led: number, on: boolean): Promise<void>;
    buttonsPressed(): Promise<number>;
}

/** Software stand-in (no hardware access), to try the UI with a bitfile that has no user I/O block. */
export class SimulatedUserIo implements IUserIo {
    private _leds = 0;
    private _buttons = 0;

    async leds(): Promise<number> {
        return this._leds;
    }
    async setLed(led: number, on: boolean): Promise<void> {
        this._leds = on ? this._leds | (led & LED_MASK) : this._leds & ~led;
    }
    async buttonsPressed(): Promise<number> {
        return this._buttons;
    }
    /** Simulates pressing / releasing a button (BUTTON_USER or BUTTON_RESET). */
    press(button: number, down: boolean): void {
        this._buttons = down ? this._buttons | (button & BUTTON_MASK) : this._buttons & ~button;
    }
}

export class UserIoDriver implements IUserIo {
    private readonly _axiLite: IAxiLite;

    constructor(axiLite: IAxiLite) {
        this._axiLite = axiLite;
    }

    /** True if the loaded bitfile contains the user I/O block (a bitfile without it answers DECERR). */
    async isPresent(): Promise<boolean> {
        try {
            await this._axiLite.read32(GPIO_BASE + GPIO2_DATA);
            return true;
        } catch (e) {
            if (e instanceof Error && e.message.includes("DECERR")) return false;
            throw e;
        }
    }

    /** LED bitmask (LED_0 | LED_1): bits set are lit. */
    async leds(): Promise<number> {
        return ((await this._axiLite.read32(GPIO_BASE + GPIO_DATA)) >>> 0) & LED_MASK;
    }

    async setLeds(mask: number): Promise<void> {
        await this._axiLite.write32(GPIO_BASE + GPIO_DATA, mask & LED_MASK);
    }

    async setLed(led: number, on: boolean): Promise<void> {
        const v = await this.leds();
        await this.setLeds(on ? v | led : v & ~led);
    }

    /** Bitmask of the buttons currently pressed (BUTTON_USER | BUTTON_RESET). */
    async buttonsPressed(): Promise<number> {
        return ~(await this._axiLite.read32(GPIO_BASE + GPIO2_DATA)) & BUTTON_MASK;
    }
}
