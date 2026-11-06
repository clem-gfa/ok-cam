/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import "./CaptureSettingsView.css";

import {
    FrameConfiguration,
    TestMode,
} from "./CameraTypes";

import SelectComponent, { SelectComponentListItem } from "./SelectComponent";
import ToggleGroupComponent from "./ToggleGroupComponent";
import SliderComponent from "./SliderComponent";

export type FrameCaptureMode = TestMode | undefined;

export enum ImageScale {
    Original,
    ScaleToFit
}

export interface CaptureSettingsViewProps {
    captureModesList: Record<string, SelectComponentListItem<FrameCaptureMode>>;
    captureDimensionsList: Record<string, SelectComponentListItem<FrameConfiguration>>;
    imageScalesList: Record<string, SelectComponentListItem<ImageScale>>;

    captureModeKey: string;
    captureDimensionsKey: string;
    imageScaleKey: string;

    onCaptureModeChange?: (key: string) => void;
    onCaptureDimensionsChange?: (key: string) => void;
    onImageScaleChange?: (key: string) => void;
    tpgMotionSpeed?: number;
    onTPGMotionSpeedChange?: (value: number) => void;
    onReset?: () => void;
}

function CaptureSettingsView(props: CaptureSettingsViewProps) {
    // Event Handlers
    const onCaptureModeChange = (value: string) => {
        props.onCaptureModeChange?.(value);
    }

    const onCaptureDimensionsChange = (value: string) => {
        props.onCaptureDimensionsChange?.(value);
    }

    const onImageScaleChange = (value: string) => {
        props.onImageScaleChange?.(value);
    }

    return (
        <div className="okCaptureSettingsPanel">
            <div className="okSelectContainer">
                <span className="okLabelText">Camera Mode</span>
                <SelectComponent
                    selectedItemKey={props.captureModeKey}
                    selectableItemList={props.captureModesList}
                    onSelectedItemChange={onCaptureModeChange} />
            </div>
            <div className="okSelectContainer">
                <span className="okLabelText">Capture Size</span>
                <SelectComponent
                    selectedItemKey={props.captureDimensionsKey}
                    selectableItemList={props.captureDimensionsList}
                    onSelectedItemChange={onCaptureDimensionsChange}/>
            </div>
            <div className="okSelectContainer">
                <span className="okLabelText">Image Size</span>
                <ToggleGroupComponent
                    selectedItemKey={props.imageScaleKey}
                    selectableItemList={props.imageScalesList}
                    onSelectedItemChange={onImageScaleChange}/>
            </div>
            {props.tpgMotionSpeed !== undefined && (
                <SliderComponent
                    label="Motion Speed"
                    value={props.tpgMotionSpeed}
                    maximumValue={255}
                    minimumValue={0}
                    stepValue={1}
                    onValueChange={(value) => props.onTPGMotionSpeedChange?.(value)}
                />
            )}
            <button
                className="okButton"
                onClick={props.onReset}>Restart Device</button>
        </div>
    )
}

export default CaptureSettingsView;
