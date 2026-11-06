/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import "./CameraSettingsView.css";

import SliderComponent from "./SliderComponent";
import SelectSliderComponent, { SelectSliderOption } from "./SelectSliderComponent";
import { ExposureUi, exposureTitleFor } from "./CameraTypes";

// The 36 discrete exposure stops. `label` is the shutter-speed DENOMINATOR and `value` is the
// exposure in ms, so every stop satisfies value_ms === 1000 / denominator — the dial is a clean
// 1/x sequence across its whole range.
//
// The last two stops used to be written '0"3' and '0"5' in photographic seconds notation (0.3 s
// and 0.5 s). Those are exactly 1/3 s and 1/2 s, so they are now written as the denominators 3
// and 2 like every other stop. That makes the sequence uniform and lets the readout render "1/x"
// for all 36 positions without special cases. The VALUES are unchanged — this is purely notation,
// nothing different reaches the sensor.
//
// Kept in sync with exposureStops() in the C++ app (Software/Cxx/gui/main_gui.cpp).
const exposureOptions: SelectSliderOption[] = [
    { label: "8000", value: 0.125 },
    { label: "6400", value: 0.15625 },
    { label: "5000", value: 0.2 },
    { label: "4000", value: 0.25 },
    { label: "3200", value: 0.3125 },
    { label: "2500", value: 0.4 },
    { label: "2000", value: 0.5 },
    { label: "1600", value: 0.625 },
    { label: "1250", value: 0.8 },
    { label: "1000", value: 1 },
    { label: "800", value: 1.25 },
    { label: "640", value: 1.5625 },
    { label: "500", value: 2 },
    { label: "400", value: 2.5 },
    { label: "320", value: 3.125 },
    { label: "250", value: 4 },
    { label: "200", value: 5 },
    { label: "160", value: 6.25 },
    { label: "125", value: 8 },
    { label: "100", value: 10 },
    { label: "80", value: 12.5 },
    { label: "60", value: 16.6667 },
    { label: "50", value: 20 },
    { label: "40", value: 25 },
    { label: "30", value: 33.3333 },
    { label: "25", value: 40 },
    { label: "20", value: 50 },
    { label: "15", value: 66.6667 },
    { label: "13", value: 76.9231 },
    { label: "10", value: 100 },
    { label: "8", value: 125 },
    { label: "6", value: 166.6667 },
    { label: "5", value: 200 },
    { label: "4", value: 250 },
    { label: "3", value: 333.3333 },
    { label: "2", value: 500 }
];

/**
 * Render the stops the way the attached sensor actually interprets them.
 *
 *  shutter (AR0330) — the value is a real integration time, so show the shutter speed as "1/x".
 *  aec (OV5640) — the value is not a time at all; PCAMCameraControl clamps it into 0..247 and
 *      writes it to the AEC stable-range registers as a luminance setpoint. Show the number that
 *      actually reaches the sensor, so the control does not claim a unit it does not have. Note
 *      this makes the clamping visible: the fastest stops all read 0 and the slowest all read
 *      247, which is the pre-existing dead-zone defect tracked separately — this display change
 *      surfaces it rather than causing it.
 *  none (TPG) — no sensor; the control is disabled, so the shutter rendering is fine.
 */
function exposureOptionsFor(ui: ExposureUi): SelectSliderOption[] {
    if (ui === "aec") {
        return exposureOptions.map((o) => ({
            label: String(Math.max(0, Math.min(247, Math.trunc(o.value)))),
            value: o.value
        }));
    }
    return exposureOptions.map((o) => ({ label: `1/${o.label}`, value: o.value }));
}

export interface CameraSettingsViewProps {
    /** Presentation for the exposure control, derived from the attached sensor. */
    exposureUi: ExposureUi;
    exposure: number;
    redGain: number;
    greenGain: number;
    blueGain: number;
    autoWhiteBalance: number;

    onExposureChange?: (value: number) => void;
    onAutoWhiteBalanceChange?: (value: number) => void;

    onRedGainChange?: (value: number) => void;
    onGreenGainChange?: (value: number) => void;
    onBlueGainChange?: (value: number) => void;
}

function CameraSettingsView(props: CameraSettingsViewProps) {

    // Event Handlers
    const onExposureChange = (value: number) => {
        props.onExposureChange?.(value);
    }

    const onAutoWhiteBalanceChange = (value: number) => {
        props.onAutoWhiteBalanceChange?.(value);
    }

    const onRedColorGainChange = (value: number) => {
        props.onRedGainChange?.(value);
    }

    const onGreenColorGainChange = (value: number) => {
        props.onGreenGainChange?.(value);
    }

    const onBlueColorGainChange = (value: number) => {
        props.onBlueGainChange?.(value);
    }

    return (
        <div className="okCameraSettingsPanel">
            <SelectSliderComponent
                label={exposureTitleFor(props.exposureUi)}
                color="#FFFFFF"
                value={props.exposure}
                options={exposureOptionsFor(props.exposureUi)}
                onValueChange={onExposureChange}
            />
            <SliderComponent
                label="Auto White Balance"
                color="#FFFFFF"
                value={props.autoWhiteBalance}
                maximumValue={255}
                minimumValue={0}
                stepValue={1}
                onValueChange={onAutoWhiteBalanceChange}
            />
            <SliderComponent
                label="Red Gain"
                color="#EB5757"
                value={props.redGain}
                maximumValue={255}
                minimumValue={0}
                stepValue={1}
                onValueChange={onRedColorGainChange}
            />
            <SliderComponent
                label="Green Gain"
                color="#44BD84"
                value={props.greenGain}
                maximumValue={255}
                minimumValue={0}
                stepValue={1}
                onValueChange={onGreenColorGainChange}
            />
            <SliderComponent
                label="Blue Gain"
                color="#3B62DA"
                value={props.blueGain}
                maximumValue={255}
                minimumValue={0}
                stepValue={1}
                onValueChange={onBlueColorGainChange}
            />
        </div>
    )
}

export default CameraSettingsView;
