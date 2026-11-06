/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React, { Component, RefObject } from "react";

import * as Dialog from "@radix-ui/react-dialog";
import * as Popover  from "@radix-ui/react-popover";
import * as Toggle from "@radix-ui/react-toggle";

import "./CameraView.css";

import CaptureSettingsIcon from "./Icons/CaptureSettingsIcon";
import CameraSettingsIcon from "./Icons/CameraSettingsIcon";
import CaptureStatusIcon from "./Icons/CaptureStatusIcon";
import HistogramIcon from "./Icons/HistogramIcon";
import ContinuousCaptureIcon from "./Icons/ContinuousCaptureIcon";
import SingleCaptureIcon from "./Icons/SingleCaptureIcon";
import ObjectDetectionIcon from "./Icons/ObjectDetectionIcon";

import DraggablePanel from "./DraggablePanel";

import CaptureSettingsView, { FrameCaptureMode, ImageScale } from "./CaptureSettingsView";
import CameraSettingsView from "./CameraSettingsView";

import CaptureStatusView from "./CaptureStatusView";
import HistogramView from "./HistogramView";
import CameraFrameView, { ObjectDetectionState } from "./CameraFrameView";

import { SelectComponentListItem } from "./SelectComponent";

import {
    FrameConfiguration,
    TestMode,
    CameraExposure,
    RedGain,
    GreenGain,
    BlueGain,
    AWB,
    CameraMode,
    exposureUiFor
} from "./CameraTypes";

import { ICameraControl } from "./ICameraControl";
import { TPGCameraControl } from "./TPGCameraControl";
import { CapturePipelineSequencer, CapturedFrame } from "./CapturePipelineSequencer";
import { IISP } from "./IISP";
import { ITPG } from "./ITPG";
import { TPG_PATTERN_PASSTHROUGH, TPG_PATTERN_HORIZONTAL_RAMP, testModeToPatternId } from "./TPGPatterns";

import { WorkQueue } from "@opalkelly/frontpanel-platform-api";

const OBJECT_DETECTION_MODEL_URL = "frontpanel://localhost/assets/models/yolo26n_web_model_320x320_int8/model.json";

export enum CameraProductModel {
    SZG_CAMERA_AR0330 = "SZG-CAMERA-AR0330",
    POD_CAMERA_AR0330 = "POD-CAMERA-AR0330",
    SZG_MIPI_8320 = "SZG-MIPI-8320"
}

/**
 * Properties for the camera view component.
 */
interface CameraViewProps {
    name: string;
    sequencer: CapturePipelineSequencer;
    isp: IISP;
    tpg: ITPG;
    cameraControl: ICameraControl;
    workQueue: WorkQueue;
    /** Attached camera, so the exposure control can present itself correctly. */
    cameraMode: CameraMode;
}

/**
 * Aggregated camera settings passed to setCameraState.
 */
type CameraSettings = {
    frameConfiguration: FrameConfiguration;
    frameCaptureMode: FrameCaptureMode;
    exposure: CameraExposure;
    rgain: RedGain;
    ggain: GreenGain;
    bgain: BlueGain;
    awb: AWB;
};

/**
 * Interface for the state of a camera view component.
 */
interface CameraViewState {
    frameDimensionsList: Record<string, SelectComponentListItem<FrameConfiguration>>;
    frameCaptureModeList: Record<string, SelectComponentListItem<FrameCaptureMode>>;
    imageScaleModeList: Record<string, SelectComponentListItem<ImageScale>>;

    selectedFrameSizeKey: string;
    selectedFrameCaptureModeKey: string;
    selectedImageScaleModeKey: string;

    exposure: number;
    rgain: number;
    ggain: number;
    bgain: number;
    awb: number;
    tpgMotionSpeed: number;

    showCaptureSettings: boolean;
    showCameraSettings: boolean;

    showCaptureStatus: boolean;
    showHistogram: boolean;

    continuousFrameCaptureState: boolean;
    objectDetectionState: ObjectDetectionState;
    showDetectionSpinner: boolean;

    imageScaleMode: ImageScale;

    topPanelId?: string;
}

const TEST_MODE_LABELS: Partial<Record<TestMode, string>> = {
    [TestMode.HorizontalRamp]: "Horizontal Ramp",
    [TestMode.VerticalRamp]: "Vertical Ramp",
    [TestMode.SolidRed]: "Solid Red",
    [TestMode.SolidGreen]: "Solid Green",
    [TestMode.SolidBlue]: "Solid Blue",
    [TestMode.TemporalRamp]: "Temporal Ramp",
    [TestMode.SolidBlack]: "Solid Black",
    [TestMode.SolidWhite]: "Solid White",
    [TestMode.CombinedRamp]: "Combined Ramp",
    [TestMode.Pseudorandom]: "Pseudorandom",
    [TestMode.DPColorRamp]: "DP Color Ramp",
    [TestMode.DPBWVertical]: "DP B/W Vertical",
    [TestMode.DPColorSquare]: "DP Color Square",
};

/**
 * Class representing a camera view component.
 */
class CameraView extends Component<CameraViewProps, CameraViewState> {
    private readonly _sequencer: CapturePipelineSequencer;
    private readonly _isp: IISP;
    private readonly _tpg: ITPG;
    private readonly _cameraControl: ICameraControl;

    private readonly _mainViewRef: RefObject<HTMLDivElement>;
    private readonly _canvasViewRef: RefObject<CameraFrameView>;
    private readonly _histogramViewRef: RefObject<HistogramView>;
    private readonly _captureStatusViewRef: RefObject<CaptureStatusView>;

    private readonly _panelZIndex: number = 10;
    private readonly _topPanelZIndex: number = 11;

    componentDidMount(): void {
        this.initialize();
    }

    componentWillUnmount(): void {
        // Fire-and-forget — React unmounts synchronously, we can't await here.
        this._sequencer.stopCapture();
    }

    constructor(props: CameraViewProps) {
        super(props);

        this._sequencer = props.sequencer;
        this._isp = props.isp;
        this._tpg = props.tpg;
        this._cameraControl = props.cameraControl;

        this._mainViewRef = React.createRef();
        this._canvasViewRef = React.createRef();
        this._histogramViewRef = React.createRef();
        this._captureStatusViewRef = React.createRef();

        // Get the list of options for the frame size, test modes, and display modes.
        const frameSizeSet: Record<string, SelectComponentListItem<FrameConfiguration>> = this.getFrameConfigurationSet();
        const frameCaptureModeSet: Record<string, SelectComponentListItem<FrameCaptureMode>> = this.getFrameCaptureModeSet();
        const imageScaleModeSet: Record<string, SelectComponentListItem<ImageScale>> = this.getImageScaleModeSet();

        const initialFrameSizeKey = Object.keys(frameSizeSet)[0];
        //const initialFrameSize = frameSizeSet[initialFrameSizeKey];

        const initialImageScaleModeKey = Object.keys(imageScaleModeSet)[0];
        const initialImageScaleMode = imageScaleModeSet[initialImageScaleModeKey];

        // Initialize the state.
        this.state = {
            frameDimensionsList: frameSizeSet,
            frameCaptureModeList: frameCaptureModeSet,
            imageScaleModeList: imageScaleModeSet,

            selectedFrameSizeKey: initialFrameSizeKey,
            selectedFrameCaptureModeKey: Object.keys(frameCaptureModeSet)[0],
            selectedImageScaleModeKey: initialImageScaleModeKey,

            exposure: 33.3333,
            rgain: 127,
            ggain: 127,
            bgain: 127,
            awb: 255,
            tpgMotionSpeed: 3,

            showCaptureSettings: false,
            showCameraSettings: false,

            showCaptureStatus: false,
            showHistogram: false,

            continuousFrameCaptureState: true,
            objectDetectionState: ObjectDetectionState.Initializing,
            showDetectionSpinner: false,

            imageScaleMode: initialImageScaleMode.value
        };
    }

    render() {
        const isTPGOnly = this._cameraControl instanceof TPGCameraControl;
        return (
            <div className="okCameraView">
                <div className="okSidebarPanel">
                    <div className="okMenuPanel">
                        <Popover.Root open={this.state.showCaptureSettings}>
                            <Popover.Anchor>
                                <Toggle.Root
                                    className="okToggleButton"
                                    pressed={this.state.showCaptureSettings}
                                    onPressedChange={(pressed) => this.setState({showCaptureSettings: pressed})}
                                >
                                    <CaptureSettingsIcon color="#FFFFFF"/>
                                </Toggle.Root>
                            </Popover.Anchor>
                            <Popover.Portal container={this._mainViewRef.current}>
                                <DraggablePanel
                                    title="Device Settings"
                                    panelId="capture-settings"
                                    container={this._mainViewRef.current}
                                    defaultPosition={{ x: 20, y: 20 }}
                                    zIndex={(this.state.topPanelId === "capture-settings") ? this._topPanelZIndex : this._panelZIndex}
                                    onActive={() => this.setState({ topPanelId: "capture-settings" })}
                                    >
                                    <CaptureSettingsView
                                        captureModesList={this.state.frameCaptureModeList}
                                        captureDimensionsList={this.state.frameDimensionsList}
                                        imageScalesList={this.state.imageScaleModeList}
                                        captureModeKey={this.state.selectedFrameCaptureModeKey}
                                        captureDimensionsKey={this.state.selectedFrameSizeKey}
                                        imageScaleKey={this.state.selectedImageScaleModeKey}
                                        onCaptureModeChange={this.onSelectedFrameCaptureModeChanged}
                                        onCaptureDimensionsChange={this.onSelectedFrameSizeChanged}
                                        onImageScaleChange={this.onSelectedImageScaleChanged}
                                        tpgMotionSpeed={
                                            this.getFrameCaptureMode(this.state.selectedFrameCaptureModeKey) !== undefined
                                                ? this.state.tpgMotionSpeed
                                                : undefined
                                        }
                                        onTPGMotionSpeedChange={this.onTPGMotionSpeedChanged}
                                        onReset={this.onResetButtonClick}
                                    />
                                </DraggablePanel>
                            </Popover.Portal>
                        </Popover.Root>
                        <Popover.Root open={!isTPGOnly && this.state.showCameraSettings}>
                            <Popover.Anchor>
                                <Toggle.Root
                                    className="okToggleButton"
                                    disabled={isTPGOnly}
                                    pressed={!isTPGOnly && this.state.showCameraSettings}
                                    onPressedChange={(pressed) => this.setState({showCameraSettings: pressed})}
                                >
                                    <CameraSettingsIcon color="#FFFFFF"/>
                                </Toggle.Root>
                            </Popover.Anchor>
                            <Popover.Portal container={this._mainViewRef.current}>
                                <DraggablePanel
                                    title="Image Settings"
                                    panelId="image-settings"
                                    container={this._mainViewRef.current}
                                    defaultPosition={{ x: 20, y: 20 }}
                                    zIndex={(this.state.topPanelId === "image-settings") ? this._topPanelZIndex : this._panelZIndex}
                                    onActive={() => this.setState({ topPanelId: "image-settings" })}
                                    >
                                        <CameraSettingsView
                                            exposureUi={exposureUiFor(this.props.cameraMode)}
                                            exposure={this.state.exposure}
                                            autoWhiteBalance={this.state.awb}
                                            redGain={this.state.rgain}
                                            greenGain={this.state.ggain}
                                            blueGain={this.state.bgain}
                                            onExposureChange={this.onExposureChanged}
                                            onAutoWhiteBalanceChange={this.onAWBChanged}
                                            onRedGainChange={this.onRGainChanged}
                                            onGreenGainChange={this.onGGainChanged}
                                            onBlueGainChange={this.onBGainChanged}
                                        />
                                </DraggablePanel>
                            </Popover.Portal>
                        </Popover.Root>
                    </div>
                    <div className="okMenuPanel">
                        <Toggle.Root
                            className="okToggleButton"
                            pressed={this.state.showCaptureStatus}
                            onPressedChange={(pressed) => this.setState({showCaptureStatus: pressed})}
                            >
                                <CaptureStatusIcon color="#FFFFFF"/>
                        </Toggle.Root>
                        <Toggle.Root
                            className="okToggleButton"
                            pressed={this.state.showHistogram}
                            onPressedChange={(pressed) => this.setState({showHistogram: pressed})}
                            >
                                <HistogramIcon color="#FFFFFF"/>
                        </Toggle.Root>
                        <Toggle.Root
                            className="okToggleButton"
                            pressed={this.state.objectDetectionState === ObjectDetectionState.Active}
                            onPressedChange={this.onObjectDetectionToggle}
                            disabled={this.state.objectDetectionState !== ObjectDetectionState.Ready && this.state.objectDetectionState !== ObjectDetectionState.Active}
                            >
                                <ObjectDetectionIcon color="#FFFFFF"/>
                        </Toggle.Root>
                    </div>
                    <div className="okMenuPanel">
                        <Toggle.Root
                            className="okToggleButton"
                            pressed={this.state.continuousFrameCaptureState}
                            onPressedChange={this.onContinuousFrameCaptureStateChanged}
                            >
                                <ContinuousCaptureIcon color="#FFFFFF"/>
                        </Toggle.Root>
                        <button
                            className="okIconButton"
                            onClick={this.onCaptureFrameButtonClick}
                            style={{ background: "#3B62DA" }}
                            disabled={this.state.continuousFrameCaptureState}
                            >
                                <SingleCaptureIcon color="#FFFFFF"/>
                        </button>
                    </div>
                </div>
                <div
                    className="okMainPanel"
                    ref={this._mainViewRef}
                    style={{
                        position: "relative",
                        display: "inline-block",
                        height: "100%",
                        flex: 1,
                        margin: 0,
                        padding: 0,
                        boxSizing: "border-box",
                        overflow: "hidden",
                    }}
                    >
                    <CameraFrameView
                        ref={this._canvasViewRef}
                        dimensions={this.getFrameConfiguration(this.state.selectedFrameSizeKey).dimensions}
                        scaleToFit={this.state.imageScaleMode === ImageScale.ScaleToFit}
                        modelUrl={OBJECT_DETECTION_MODEL_URL}
                        onObjectDetectionStateChange={this.onObjectDetectionStateChanged}
                        onFirstInferenceComplete={this.onFirstInferenceComplete}
                    />
                    {this.state.showCaptureStatus && (
                        <div
                            style={{
                                position: 'absolute',
                                top: '0px',  // 22px padding + 10px margin
                                right: '0px',   // 10px padding + 10px margin
                                pointerEvents: 'auto', // Ensure it's interactive
                            }}
                        >
                            <CaptureStatusView ref={this._captureStatusViewRef} />
                        </div>
                    )}
                    {this.state.showHistogram && (
                        <div
                            style={{
                                position: 'absolute',
                                bottom: '0px',  // 22px padding + 10px margin
                                right: '0px',   // 10px padding + 10px margin
                                pointerEvents: 'auto', // Ensure it's interactive
                            }}
                        >
                            <HistogramView
                                ref={this._histogramViewRef}
                                width={768}
                                height={200}
                            />
                        </div>
                    )}
                </div>
                <Dialog.Root open={this.state.showDetectionSpinner}>
                    <Dialog.Portal>
                        <Dialog.Overlay className="DialogOverlay" />
                        <Dialog.Content className="DialogContent" onOpenAutoFocus={(e) => e.preventDefault()}>
                            <Dialog.Title className="DialogTitle">Preparing Object Detection</Dialog.Title>
                            <div className="DialogBody">
                                <div className="SpinnerContainer">
                                    <div className="Spinner" />
                                </div>
                                <div className="ProgressText">Compiling shaders...</div>
                            </div>
                        </Dialog.Content>
                    </Dialog.Portal>
                </Dialog.Root>
            </div>
        );
    }

    /**
     * Initializes the camera device.
     */
    private async initialize() {
        await this.props.workQueue.post(async (): Promise<void> => {
            // Stop pipeline (soft, I2C stays valid) before sensor init
            await this._sequencer.assertPipelineResets();

            // Sensor init (no pipeline reset)
            await this._cameraControl.initialize();

            // Pipeline init
            await this._sequencer.initializePipeline();
        });

        // Initialize the Camera state.
        const frameConfiguration: FrameConfiguration = this.getFrameConfiguration(
            this.state.selectedFrameSizeKey
        );
        const frameCaptureMode: FrameCaptureMode = this.getFrameCaptureMode(
            this.state.selectedFrameCaptureModeKey
        );

        await this.setCameraState({
            frameConfiguration,
            frameCaptureMode,
            exposure: this.state.exposure,
            rgain: this.state.rgain,
            ggain: this.state.ggain,
            bgain: this.state.bgain,
            awb: this.state.awb,
        });

        if (this.state.continuousFrameCaptureState) {
            this._sequencer.startCapture(this.handleFrame);
        }
    }

    /**
     * Single fan-out point for captured frames. Called by the sequencer's
     * capture loop (continuous mode) and by onCaptureFrameButtonClick
     * (single-shot mode). Updates canvas, histogram chart, and FPS counters
     * directly via refs.
     */
    private handleFrame = (frame: CapturedFrame): void => {
        const detectionActive = this.state.objectDetectionState === ObjectDetectionState.Active;
        this._canvasViewRef.current?.updateFrameImage(frame, detectionActive);
        this._histogramViewRef.current?.updateHistogram(frame.histogram);
        this._captureStatusViewRef.current?.updateStats(frame);
    };

    /**
     * Sets the state of the camera device.
     */
    private async setCameraState(settings: CameraSettings) {
        const { frameConfiguration, frameCaptureMode,
                exposure, rgain, ggain, bgain, awb } = settings;

        await this.props.workQueue.post(async (): Promise<void> => {
            // Set exposure on camera sensor (camera control caches for re-apply after pipeline reset)
            await this._cameraControl.setExposure(exposure);

            console.log("Set Exposure: " + exposure);
            console.log("Set RGain: " + rgain);
            console.log("Set GGain: " + ggain);
            console.log("Set BGain: " + bgain);
            console.log("Set Thresh: " + awb);

            await this._isp.setGains(rgain, ggain, bgain);
            await this._isp.setAWBThreshold(awb);

            await this._cameraControl.setSize(frameConfiguration.dimensions);

            console.log(
                "Set Size: columnCount=" +
                    frameConfiguration.dimensions.columnCount +
                    " rowCount=" +
                    frameConfiguration.dimensions.rowCount
            );

            await this._cameraControl.setSkips(frameConfiguration.skips);

            console.log(
                "Set Skips: columnCount=" +
                    frameConfiguration.skips.columnCount +
                    " rowCount =" +
                    frameConfiguration.skips.rowCount
            );

            const defaultPattern = this._cameraControl instanceof TPGCameraControl
                ? TPG_PATTERN_HORIZONTAL_RAMP
                : TPG_PATTERN_PASSTHROUGH;

            if (frameCaptureMode === undefined) {
                await this._tpg.setPattern(defaultPattern);
                console.log("Set Frame Capture Mode: Image Capture");
            } else {
                await this._tpg.setPattern(testModeToPatternId(frameCaptureMode, defaultPattern));
                console.log("Set Frame Capture Mode: " + frameCaptureMode);
            }

            // Sync pipeline resolution and frame dimensions from camera control
            const dims = this._cameraControl.frameDimensions;
            this._sequencer.setResolution(dims.columnCount, dims.rowCount);
            await this._sequencer.setFrameDimensions(dims);

            // Pipeline reset: stop + reinitializeI2C + configure + start + re-apply exposure
            await this._sequencer.logicReset();
        });
    }

    /**
     * Generates the set of selectable frame configuration items that are supported.
     * @returns The set of selectable frame configuration items.
     */
    private getFrameConfigurationSet(): Record<string, SelectComponentListItem<FrameConfiguration>> {
        const frameConfigKeyValuePairs = this._cameraControl.supportedFrameConfigurations.map(
            (frameConfig) => {
                const frameConfigKey: string = `dimensions-${frameConfig.dimensions.columnCount}x${frameConfig.dimensions.rowCount}`;
                const frameConfigName: string = `${frameConfig.dimensions.columnCount}x${frameConfig.dimensions.rowCount}`
                const frameConfigItem = new SelectComponentListItem(frameConfigName, frameConfig);
                return [frameConfigKey, frameConfigItem];
            }
        );

        return Object.fromEntries(frameConfigKeyValuePairs);
    }

    /**
     * Generates the set of selectable frame capture mode items for all
     * supported modes.
     * @returns The set of selectable frame capture mode items.
     */
    private getFrameCaptureModeSet(): Record<string, SelectComponentListItem<FrameCaptureMode>> {
        const testModeKeyValuePairs = this._cameraControl.supportedTestModes.map(
            (testMode) => {
                const testModeKey: string = `testmode-${testMode}`;
                const testModeName: string = this.getFrameCaptureTestModeLabel(testMode);
                const testModeItem = new SelectComponentListItem(testModeName, testMode);
                return [testModeKey, testModeItem];
            }
        );

        if (this._cameraControl instanceof TPGCameraControl) {
            return Object.fromEntries(testModeKeyValuePairs);
        }

        const imageCaptureItem = new SelectComponentListItem("Image Capture", undefined);
        const allModeKeyValuePairs = [
            ["mode-imagecapture", imageCaptureItem],
            ...testModeKeyValuePairs
        ];

        return Object.fromEntries(allModeKeyValuePairs);
    }

    /**
     * Generates the set of selectable image scale items.
     * @returns The set of selectable image scale items.
     */
    private getImageScaleModeSet(): Record<string, SelectComponentListItem<ImageScale>> {
        const imageScaleKeyValuePairs = [
            ["scale-fit", new SelectComponentListItem("Scale", ImageScale.ScaleToFit)],
            ["scale-original", new SelectComponentListItem("1:1", ImageScale.Original)]
        ];

        return Object.fromEntries(imageScaleKeyValuePairs);
    }

    private getFrameCaptureTestModeLabel(mode: TestMode): string {
        return TEST_MODE_LABELS[mode] ?? "Unknown";
    }

    /**
     * Retrieves the frame configuration for a specific key.
     * @param key - The key of the target frame configuration.
     * @returns The frame configuration corresponding to the key.
     */
    private getFrameConfiguration(key: string): FrameConfiguration {
        const selectedItem = this.state.frameDimensionsList[key];

        return selectedItem?.value;
    }

    /**
     * Retrieves the frame capture mode that is currently selected.
     * @returns The frame capture mode that is selected.
     */
    private getFrameCaptureMode(key: string): FrameCaptureMode {
        const selectedItem = this.state.frameCaptureModeList[key];

        return selectedItem?.value;
    }

    /**
     * Retrieves the image scale mode that is currently selected.
     * @returns The image scale mode that is selected.
     */
    private getImageScaleMode(key: string): ImageScale {
        const selectedItem = this.state.imageScaleModeList[key];

        return selectedItem?.value;
    }

    // Value Change Event Handlers
    private onContinuousFrameCaptureStateChanged = async (checked: boolean) => {
        console.log("Continuous Capture State Changed: " + checked);

        if (checked) {
            this.setState({ continuousFrameCaptureState: checked });
            this._sequencer.startCapture(this.handleFrame);
        } else {
            await this._sequencer.stopCapture();
            this.setState({ continuousFrameCaptureState: checked });
        }
    };

    /**
     * Selected Frame Size Changed Event Handler.
     * @param name - The name of the selected frame size.
     */
    private onSelectedFrameSizeChanged = (key: string) => {
        console.log("Selected Frame Changed: " + key);

        const frameConfiguration: FrameConfiguration = this.getFrameConfiguration(key);
        const frameCaptureMode: FrameCaptureMode = this.getFrameCaptureMode(
            this.state.selectedFrameCaptureModeKey
        );

        this.setCameraState({
            frameConfiguration,
            frameCaptureMode,
            exposure: this.state.exposure,
            rgain: this.state.rgain,
            ggain: this.state.ggain,
            bgain: this.state.bgain,
            awb: this.state.awb,
        });

        this.setState({ selectedFrameSizeKey: key });
    };

    /**
     * Selected Frame Capture Mode Changed Event Handler.
     * @param name - The name of the selected frame capture mode.
     */
    private onSelectedFrameCaptureModeChanged = (key: string) => {
        console.log("Selected Frame Capture Mode Changed: " + key);

        const frameCaptureMode: FrameCaptureMode = this.getFrameCaptureMode(key);

        // Test mode changes only write a TPG register. No pipeline restart needed.
        this.props.workQueue.post(async () => {
            const defaultPattern = this._cameraControl instanceof TPGCameraControl
                ? TPG_PATTERN_HORIZONTAL_RAMP
                : TPG_PATTERN_PASSTHROUGH;

            if (frameCaptureMode === undefined) {
                await this._tpg.setPattern(defaultPattern);
                console.log("Set Frame Capture Mode: Image Capture");
            } else {
                await this._tpg.setPattern(testModeToPatternId(frameCaptureMode, defaultPattern));
                console.log("Set Frame Capture Mode: " + frameCaptureMode);
            }
        });

        this.setState({ selectedFrameCaptureModeKey: key });
    };

    /**
     * Selected Image Scale Changed Event Handler.
     * @param name - The name of the selected image scale.
     */
    private onSelectedImageScaleChanged = (key: string) => {
        console.log("Selected Image Scale Changed: " + key);

        const imageScaleMode = this.getImageScaleMode(key);

        this.setState({ selectedImageScaleModeKey: key, imageScaleMode: imageScaleMode });
    };

    /**
     * Exposure Changed Event Handler.
     * @param value - The selected exposure value.
     */
    private onExposureChanged = (value: number) => {
        console.log("Selected Exposure Changed: " + value);

        this.props.workQueue.post(async () => {
            await this._cameraControl.setExposure(value);
        });

        this.setState({ exposure: Number(value) });
    };

    private setISPParameter = (key: "rgain" | "ggain" | "bgain" | "awb", value: number) => {
        console.log(`${key} Changed: ${value}`);

        const params = {
            rgain: this.state.rgain,
            ggain: this.state.ggain,
            bgain: this.state.bgain,
            awb: this.state.awb,
            [key]: value
        };

        this.props.workQueue.post(async () => {
            await this._isp.setGains(params.rgain, params.ggain, params.bgain);
            await this._isp.setAWBThreshold(params.awb);
        });

        this.setState({ [key]: value } as Pick<CameraViewState, typeof key>);
    };

    private onRGainChanged = (value: number) => this.setISPParameter("rgain", value);
    private onGGainChanged = (value: number) => this.setISPParameter("ggain", value);
    private onBGainChanged = (value: number) => this.setISPParameter("bgain", value);
    private onAWBChanged = (value: number) => this.setISPParameter("awb", value);

    private onObjectDetectionToggle = (pressed: boolean) => {
        if (pressed) {
            // Show spinner first, then enable detection after the spinner has painted.
            // WebGL shader compilation blocks the main thread, so we must ensure
            // the spinner is visible before starting.
            this.setState({ showDetectionSpinner: true }, () => {
                requestAnimationFrame(() => {
                    setTimeout(() => {
                        this.setState({ objectDetectionState: ObjectDetectionState.Active }, () => {
                            // In picture mode (continuous capture off), no new frames arrive,
                            // so we must explicitly trigger detection on the existing canvas
                            // content to complete the first inference and dismiss the spinner.
                            if (!this.state.continuousFrameCaptureState) {
                                this._canvasViewRef.current?.triggerDetection();
                            }
                        });
                    }, 0);
                });
            });
        } else {
            this.setState({ objectDetectionState: ObjectDetectionState.Ready });
            this._canvasViewRef.current?.updateFrameImage(null, false);
        }
    };

    private onFirstInferenceComplete = () => {
        this.setState({ showDetectionSpinner: false });
    };

    private onObjectDetectionStateChanged = (state: ObjectDetectionState) => {
        this.setState({ objectDetectionState: state });
    };
    /**
     * TPG Motion Speed Changed Event Handler.
     * @param value - The selected motion speed value (0–255).
     */
    private onTPGMotionSpeedChanged = (value: number) => {
        this.props.workQueue.post(async () => {
            await this._tpg.setMotionSpeed(value);
        });

        this.setState({ tpgMotionSpeed: value });
    };

    /**
     * Reset Button Click Event Handler.
     */
    private onResetButtonClick = async () => {
        // Stop the capture loop (awaits the current iteration) before
        // reinitializing. Without this, a queued frame capture would
        // run on a stopped pipeline, causing a hang or 5s timeout.
        await this._sequencer.stopCapture();
        await this.initialize();
    };

    /**
     * Capture Frame Button Click Event Handler.
     */
    private onCaptureFrameButtonClick = async () => {
        const startTimeStamp: number = performance.now();

        const frame = await this._sequencer.captureOnce();

        if (frame) {
            console.log(
                "Captured Single Frame: ColumnCount=" +
                    frame.width +
                    " RowCount=" +
                    frame.height +
                    " Size=" +
                    frame.image.byteLength
            );
            this.handleFrame(frame);
        } else {
            console.error("Failed to Capture Single Frame");
        }

        const elapsedTime: number = performance.now() - startTimeStamp;

        console.log("Captured Single Frame: Elapsed=" + elapsedTime + "ms");
    };
}

export default CameraView;
