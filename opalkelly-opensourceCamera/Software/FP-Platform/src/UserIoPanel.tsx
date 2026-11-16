/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Small test/demo panel for the DARWIN user I/O: two LED toggles and the live state of the two push buttons.
// The same UserIoDriver is what future application code (e.g. "button starts a recording") would use.

import React from "react";

import { WorkQueue } from "@opalkelly/frontpanel-platform-api";

import { IAxiLite } from "./IAxi";
import { BUTTON_RESET, BUTTON_USER, LED_0, LED_1, UserIoDriver } from "./UserIoDriver";

interface UserIoPanelProps {
    axiLite: IAxiLite;
    workQueue: WorkQueue;
}

const UserIoPanel: React.FC<UserIoPanelProps> = ({ axiLite, workQueue }) => {
    const [open, setOpen] = React.useState(false);
    const [present, setPresent] = React.useState<boolean | null>(null);
    const [leds, setLeds] = React.useState(0);
    const [buttons, setButtons] = React.useState(0);
    const [message, setMessage] = React.useState("");
    const driver = React.useMemo(() => new UserIoDriver(axiLite), [axiLite]);

    // Presence check, then poll the buttons (4 Hz) while the panel is open
    React.useEffect(() => {
        if (!open) return;
        let cancelled = false;
        const poll = async () => {
            try {
                await workQueue.post(async () => {
                    if (present === null) {
                        const here = await driver.isPresent();
                        if (!cancelled) setPresent(here);
                        if (!here) return;
                    }
                    if (present === false) return;
                    const b = await driver.buttonsPressed();
                    const l = await driver.leds();
                    if (!cancelled) {
                        setButtons(b);
                        setLeds(l);
                    }
                });
            } catch (e) {
                if (!cancelled) setMessage(e instanceof Error ? e.message : String(e));
            }
        };
        void poll();
        const timer = setInterval(() => void poll(), 250);
        return () => {
            cancelled = true;
            clearInterval(timer);
        };
    }, [open, present, driver, workQueue]);

    const toggleLed = async (led: number) => {
        try {
            const on = (leds & led) === 0;
            await workQueue.post(async () => driver.setLed(led, on));
            setLeds(on ? leds | led : leds & ~led);
        } catch (e) {
            setMessage(e instanceof Error ? e.message : String(e));
        }
    };

    const dot = (on: boolean, color: string): React.CSSProperties => ({
        display: "inline-block",
        width: 12,
        height: 12,
        borderRadius: 6,
        marginRight: 6,
        verticalAlign: "middle",
        background: on ? color : "#333",
        border: "1px solid #666",
    });

    return (
        <>
            <button
                onClick={() => setOpen(!open)}
                style={{ position: "fixed", left: 190, bottom: 8, zIndex: 100, padding: "4px 8px" }}
            >
                I/O
            </button>
            {open && (
                <div
                    style={{
                        position: "fixed",
                        left: 8,
                        bottom: 44,
                        zIndex: 100,
                        background: "rgba(16,20,24,0.94)",
                        color: "#ddd",
                        font: "12px monospace",
                        padding: 8,
                    }}
                >
                    <div style={{ marginBottom: 6 }}>
                        <b>Board user I/O</b>
                    </div>
                    {present === null && <div>Checking the bitfile...</div>}
                    {present === false && (
                        <div>The loaded bitfile has no user I/O block (build the gateware with <code>set with_userio 1</code>).</div>
                    )}
                    {present && (
                        <>
                            <div style={{ marginBottom: 4 }}>
                                <button onClick={() => void toggleLed(LED_0)} style={{ marginRight: 6 }}>
                                    <span style={dot((leds & LED_0) !== 0, "#ff4d4d")} />
                                    LED_0
                                </button>
                                <button onClick={() => void toggleLed(LED_1)}>
                                    <span style={dot((leds & LED_1) !== 0, "#ff4d4d")} />
                                    LED_1
                                </button>
                            </div>
                            <div>
                                <span style={dot((buttons & BUTTON_USER) !== 0, "#5ad1a0")} />
                                User button
                                <span style={{ display: "inline-block", width: 12 }} />
                                <span style={dot((buttons & BUTTON_RESET) !== 0, "#5ad1a0")} />
                                Reset button
                            </div>
                        </>
                    )}
                    {message && <div style={{ color: "#ffb86b", marginTop: 4 }}>{message}</div>}
                </div>
            )}
        </>
    );
};

export default UserIoPanel;
