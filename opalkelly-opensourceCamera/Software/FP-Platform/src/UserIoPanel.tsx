/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Small test/demo panel for the DARWIN user I/O: two LED toggles and the live state of the two push buttons.
// The same UserIoDriver is what future application code (e.g. "button starts a recording") would use.
// With a bitfile that has no user I/O block (built without `set with_userio 1`) the panel says so, does NOT
// retry in the background (a failing AXI access can take seconds and would stall the video), and offers a
// simulated mode to try the UI.

import React from "react";

import { WorkQueue } from "@opalkelly/frontpanel-platform-api";

import { IAxiLite } from "./IAxi";
import { BUTTON_RESET, BUTTON_USER, IUserIo, LED_0, LED_1, SimulatedUserIo, UserIoDriver } from "./UserIoDriver";

interface UserIoPanelProps {
    axiLite: IAxiLite;
    workQueue: WorkQueue;
}

type Mode = "checking" | "hardware" | "absent" | "simulated";

const UserIoPanel: React.FC<UserIoPanelProps> = ({ axiLite, workQueue }) => {
    const [open, setOpen] = React.useState(false);
    const [mode, setMode] = React.useState<Mode>("checking");
    const [leds, setLeds] = React.useState(0);
    const [buttons, setButtons] = React.useState(0);
    const [message, setMessage] = React.useState("");
    const hardware = React.useMemo(() => new UserIoDriver(axiLite), [axiLite]);
    const simulated = React.useRef(new SimulatedUserIo());
    const checked = React.useRef(false);

    const io: IUserIo | null = mode === "hardware" ? hardware : mode === "simulated" ? simulated.current : null;

    // One presence check per session of the app (re-run with the Retry button), never repeated automatically.
    const checkPresence = React.useCallback(async () => {
        setMode("checking");
        setMessage("");
        try {
            let here = false;
            await workQueue.post(async () => {
                here = await hardware.isPresent();
            });
            setMode(here ? "hardware" : "absent");
        } catch (e) {
            setMode("absent");
            setMessage(`Presence check failed: ${e instanceof Error ? e.message : String(e)}`);
        }
    }, [hardware, workQueue]);

    React.useEffect(() => {
        if (open && !checked.current) {
            checked.current = true;
            void checkPresence();
        }
    }, [open, checkPresence]);

    // Poll the buttons / LEDs (4 Hz) while the panel is open and a backend is available
    React.useEffect(() => {
        if (!open || !io) return;
        let cancelled = false;
        let failures = 0;
        const read = async () => {
            const b = await io.buttonsPressed();
            const l = await io.leds();
            if (!cancelled) {
                setButtons(b);
                setLeds(l);
            }
        };
        const poll = async () => {
            try {
                if (mode === "hardware") await workQueue.post(read);
                else await read();
                failures = 0;
            } catch (e) {
                if (cancelled) return;
                setMessage(e instanceof Error ? e.message : String(e));
                if (++failures >= 3) {
                    // stop hammering the device
                    cancelled = true;
                    clearInterval(timer);
                    setMode("absent");
                }
            }
        };
        const timer = setInterval(() => void poll(), 250);
        void poll();
        return () => {
            cancelled = true;
            clearInterval(timer);
        };
    }, [open, io, mode, workQueue]);

    const toggleLed = async (led: number) => {
        if (!io) return;
        try {
            const on = (leds & led) === 0;
            if (mode === "hardware") await workQueue.post(async () => io.setLed(led, on));
            else await io.setLed(led, on);
            setLeds(on ? leds | led : leds & ~led);
        } catch (e) {
            setMessage(e instanceof Error ? e.message : String(e));
        }
    };

    const press = (button: number, down: boolean) => {
        if (mode === "simulated") simulated.current.press(button, down);
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
                        {mode === "simulated" && <span style={{ color: "#ffb86b" }}> (simulated, no hardware access)</span>}
                    </div>
                    {mode === "checking" && <div>Checking the bitfile...</div>}
                    {mode === "absent" && (
                        <div>
                            <div>
                                The loaded bitfile has no user I/O block (build the gateware with <code>set with_userio 1</code>).
                            </div>
                            <div style={{ marginTop: 6 }}>
                                <button style={{ marginRight: 6 }} onClick={() => setMode("simulated")}>
                                    Try the UI in simulated mode
                                </button>
                                <button onClick={() => void checkPresence()}>Retry</button>
                            </div>
                        </div>
                    )}
                    {(mode === "hardware" || mode === "simulated") && (
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
                                <span
                                    onMouseDown={() => press(BUTTON_USER, true)}
                                    onMouseUp={() => press(BUTTON_USER, false)}
                                    onMouseLeave={() => press(BUTTON_USER, false)}
                                    style={{ cursor: mode === "simulated" ? "pointer" : "default" }}
                                >
                                    <span style={dot((buttons & BUTTON_USER) !== 0, "#5ad1a0")} />
                                    User button
                                </span>
                                <span style={{ display: "inline-block", width: 12 }} />
                                <span
                                    onMouseDown={() => press(BUTTON_RESET, true)}
                                    onMouseUp={() => press(BUTTON_RESET, false)}
                                    onMouseLeave={() => press(BUTTON_RESET, false)}
                                    style={{ cursor: mode === "simulated" ? "pointer" : "default" }}
                                >
                                    <span style={dot((buttons & BUTTON_RESET) !== 0, "#5ad1a0")} />
                                    Reset button
                                </span>
                            </div>
                            {mode === "simulated" && <div style={{ marginTop: 4, color: "#9aa5b1" }}>Hold a button label with the mouse to simulate a press.</div>}
                        </>
                    )}
                    {message && <div style={{ color: "#ffb86b", marginTop: 4 }}>{message}</div>}
                </div>
            )}
        </>
    );
};

export default UserIoPanel;
