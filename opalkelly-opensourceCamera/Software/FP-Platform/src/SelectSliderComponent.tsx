/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as Slider from "@radix-ui/react-slider";

import "./SelectSliderComponent.css";

export interface SelectSliderOption {
    label: string,
    value: number
}

export interface SelectSliderComponentProps {
    label: string,
    color?: string;
    value: number,
    options: SelectSliderOption[],
    onValueChange: (value: number) => void
}

function SelectSliderComponent(props: SelectSliderComponentProps) {
    // Event Handlers
    const handleSliderChange = (indexes: number[]) => {
        const newValue = props.options[indexes[0]].value
        props.onValueChange?.(newValue);
    };

    let indexValue = props.options.findIndex((option: SelectSliderOption) => props.value <= option.value);

    if (indexValue === -1) {
        indexValue = 0;
    }

    return (
        <div className="okSliderPanel">
            <div className="okRowPanel">
                <span className="okLabelText">{props.label}</span>
                <div className="okValueBox">
                    {props.options[indexValue].label}
                </div>
            </div>
            <div className="okSliderContainer">
                <Slider.Root
                    className="okSliderRoot"
                    value={[indexValue]}
                    max={props.options.length - 1}
                    min={0}
                    step={1}
                    onValueChange={handleSliderChange}>
                    <Slider.Track className="okSliderTrack">
                        <Slider.Range className="okSliderRange" style={{ backgroundColor: props.color || "#FFFFFF"}}/>
                    </Slider.Track>
                    <Slider.Thumb className="okSliderThumb">
                        <svg
                            xmlns="http://www.w3.org/2000/svg"
                            width="7"
                            height="15"
                            viewBox="0 0 7 15"
                            fill="none">
                            <path d="M1 0.5H6C6.27614 0.5 6.5 0.723858 6.5 1V10.5859C6.49996 10.7185 6.44725 10.8457 6.35352 10.9395L3.85352 13.4395C3.65827 13.6346 3.34173 13.6346 3.14648 13.4395L0.646484 10.9395C0.552751 10.8457 0.50004 10.7185 0.5 10.5859V1C0.5 0.723858 0.723857 0.5 1 0.5Z" fill="#343434" stroke="#444444"/>
                        </svg>
                    </Slider.Thumb>
                </Slider.Root>
            </div>
        </div>
    );
}

export default SelectSliderComponent;
