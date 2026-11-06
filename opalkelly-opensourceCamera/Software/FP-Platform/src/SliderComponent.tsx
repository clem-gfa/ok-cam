/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as Slider from "@radix-ui/react-slider";

import "./SliderComponent.css";

export interface SliderComponentProps {
    label: string,
    color?: string;
    value: number,
    maximumValue: number,
    minimumValue: number,
    stepValue: number,
    onValueChange: (value: number) => void
}

function SliderComponent(props: SliderComponentProps) {
    const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
        const numericValue = parseInt(e.target.value);

        if (!isNaN(numericValue)) {
            const clampedValue = Math.max(
                props.minimumValue,
                Math.min(props.maximumValue, numericValue)
            );
            props.onValueChange(clampedValue);
        }
    };

    return (
        <div className="okSliderPanel">
            <div className="okRowPanel">
                <span className="okLabelText">{props.label}</span>
                <input
                    className="okEditBoxInput"
                    type="text"
                    value={props.value}
                    max={props.maximumValue}
                    min={props.minimumValue}
                    onChange={handleInputChange}
                    spellCheck="false"
                />
            </div>
            <div className="okSliderContainer">
                <Slider.Root
                    className="okSliderRoot"
                    value={[props.value]}
                    max={props.maximumValue}
                    min={props.minimumValue}
                    step={props.stepValue}
                    onValueChange={(value) => props.onValueChange(value[0])}>
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

export default SliderComponent;
