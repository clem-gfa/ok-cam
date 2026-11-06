/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React from "react";

import * as AlertDialog from "@radix-ui/react-alert-dialog";
import * as Dialog from "@radix-ui/react-dialog";
import * as Progress from "@radix-ui/react-progress";

import {
    IDeviceManager,
    IDevice,
    IDeviceInfo,
    IFPGAConfiguration,
    WorkQueue,
    DataProgressCallback,
    ByteCount,
    IEventSubscription,
    DeviceInterfaceType
} from "@opalkelly/frontpanel-platform-api";

import "./App.css";

import FrontPanelLogo from "../assets/images/logo512.png";

import CameraView, { CameraProductModel } from "./CameraView";
import { CameraMode } from "./CameraTypes";

import { IISP } from "./IISP";
import { ITPG } from "./ITPG";
import { ICameraControl } from "./ICameraControl";

import { IAxiLite, IAxiStream } from "./IAxi";
import { AxiLiteOverAxiDataPort, AxiStreamOverAxiDataPort } from "./AxiOverAxiDataPort";
import { AxiLiteOverClassicDataPort, AxiStreamOverClassicDataPort } from "./AxiOverClassicDataPort";

import { ISPDriver } from "./ISPDriver";
import { NullISPDriver } from "./NullISPDriver";
import { TPGDriver } from "./TPGDriver";
import { CapturePipelineSequencer } from "./CapturePipelineSequencer";
import { SYZYGYCameraControl } from "./SYZYGYCameraControl";
import { PCAMCameraControl } from "./PCAMCameraControl";
import { TPGCameraControl } from "./TPGCameraControl";

interface ProgressProperties { completed: number; total: number; }
interface ErrorProperties { title: string, description: string, details: string, solution: string };

enum DeviceConfiguration {
    /** XEM8320-AU25P with SZG-Camera (Classic-to-AXI bridge, szgcam bitfile) */
    XEM8320_SZG_Camera,
    /** XEM8320-AU25P with PCAM (Classic-to-AXI bridge, pcam bitfile) */
    XEM8320_PCAM,
    /** XEM8320-AU25P without camera (Classic-to-AXI bridge, nocam bitfile, TPG mode) */
    XEM8320_TPG,
    /** SZG-HUB1450-AU10P with SZG-Camera (AXI driver, szgcam bitfile) */
    HUB1450_SZG_Camera,
    /** SZG-HUB1450-AU10P with PCAM (AXI driver, pcam bitfile) */
    HUB1450_PCAM,
    /** SZG-HUB1450-AU10P without camera (AXI driver, nocam bitfile, TPG mode) */
    HUB1450_TPG,
}

const BITFILE_MAP: Record<DeviceConfiguration, string> = {
    [DeviceConfiguration.XEM8320_SZG_Camera]: "XEM8320-AU25P/camera_szgcam.bit",
    [DeviceConfiguration.XEM8320_PCAM]: "XEM8320-AU25P/camera_pcam.bit",
    [DeviceConfiguration.XEM8320_TPG]: "XEM8320-AU25P/camera_nocam.bit",
    [DeviceConfiguration.HUB1450_SZG_Camera]: "SZG-HUB1450-AU10P/camera_szgcam.bit",
    [DeviceConfiguration.HUB1450_PCAM]: "SZG-HUB1450-AU10P/camera_pcam.bit",
    [DeviceConfiguration.HUB1450_TPG]: "SZG-HUB1450-AU10P/camera_nocam.bit",
};

const DeviceWorkQueue = new WorkQueue();

const productModelToCameraModel = (modelName: string | undefined): CameraProductModel | undefined => {
    if (
        modelName === CameraProductModel.SZG_CAMERA_AR0330 ||
        modelName === CameraProductModel.POD_CAMERA_AR0330 ||
        modelName === CameraProductModel.SZG_MIPI_8320
    ) {
        return modelName as CameraProductModel;
    }

    return undefined;
};

const CAMERA_MODE_MAP: Record<DeviceConfiguration, CameraMode> = {
    [DeviceConfiguration.XEM8320_SZG_Camera]: "szgcam",
    [DeviceConfiguration.XEM8320_PCAM]: "pcam",
    [DeviceConfiguration.XEM8320_TPG]: "tpg",
    [DeviceConfiguration.HUB1450_SZG_Camera]: "szgcam",
    [DeviceConfiguration.HUB1450_PCAM]: "pcam",
    [DeviceConfiguration.HUB1450_TPG]: "tpg",
};

type CameraSystem = {
    sequencer: CapturePipelineSequencer;
    isp: IISP;
    tpg: ITPG;
    control: ICameraControl;
    mode: CameraMode;
};

function App() {
    const [cameraSystem, setCameraSystem] = React.useState<CameraSystem>();
    const [progress, setProgress] = React.useState<ProgressProperties>();
    const [error, setError] = React.useState<ErrorProperties>();

    React.useEffect(() => {
        /**
         * Loads the specified configuration file into the FPGA on the specified device.
         * @param filename - The name of the configuration file to load.
         * @param fpgaConfiguration - The fpga configuration interface to use for loading.
         */
        const loadFPGAConfiguration = async (filename: string, fpgaConfiguration: IFPGAConfiguration): Promise<void> => {
            let response;

            try {
                response = await fetch("frontpanel://localhost/assets/bitfiles/" + filename);
            }
            catch (error) {
                throw new Error(`Failed to retrieve ${filename}`);
            }

            if (!response.ok) {
                throw new Error(`Failed to fetch ${filename}: HTTP ${response.status}`);
            }

            const arrayBuffer = await response.arrayBuffer();

            const reportProgress: DataProgressCallback = (total: ByteCount, completed: ByteCount) => {
                setProgress({ completed, total });
            };

            await fpgaConfiguration.loadConfigurationFromMemory(arrayBuffer, reportProgress);
        };

        /**
         * Initializes the device by configuring the FPGA on the device with the file
         * corresponding to the product name of the device.
         * @param device - The device to initialize.
         * @returns {Promise<IDevice>} A promise that resolves to the opened device.
         */
        const initializeDevice = async (device: IDevice, config: DeviceConfiguration): Promise<void> => {
            const filename = BITFILE_MAP[config];
            console.log(`Initializing Device: loading ${filename}`);
            const fpgaConfiguration = device.getFPGAConfiguration();
            await loadFPGAConfiguration(filename, fpgaConfiguration);
        };

        const determineConfiguration = (
            cameraPeripheral: CameraProductModel | undefined,
            isGen3: boolean
        ): DeviceConfiguration | null => {
            if (cameraPeripheral) {
                if (isGen3) {
                    if (cameraPeripheral === CameraProductModel.SZG_MIPI_8320) return DeviceConfiguration.HUB1450_PCAM;
                    return DeviceConfiguration.HUB1450_SZG_Camera;
                }
                if (cameraPeripheral === CameraProductModel.SZG_MIPI_8320) return DeviceConfiguration.XEM8320_PCAM;
                return DeviceConfiguration.XEM8320_SZG_Camera;
            }
            if (isGen3) return DeviceConfiguration.HUB1450_TPG;
            return DeviceConfiguration.XEM8320_TPG;
        };

        const loadAxiInterfaces = async (device: IDevice, config: DeviceConfiguration): Promise<{ axiLite: IAxiLite; axiStream: IAxiStream }> => {
            const mode = CAMERA_MODE_MAP[config];

            switch (config) {
                case DeviceConfiguration.XEM8320_SZG_Camera:
                case DeviceConfiguration.XEM8320_PCAM:
                case DeviceConfiguration.XEM8320_TPG: {
                    console.log(`Loading Classic data port (${mode})`);
                    const classicDataPort = await device.getFPGADataPortClassic();
                    return {
                        axiLite: new AxiLiteOverClassicDataPort(classicDataPort),
                        axiStream: new AxiStreamOverClassicDataPort(classicDataPort),
                    };
                }
                case DeviceConfiguration.HUB1450_SZG_Camera:
                case DeviceConfiguration.HUB1450_PCAM:
                case DeviceConfiguration.HUB1450_TPG: {
                    console.log(`Loading AXI data port (${mode})`);
                    const axiDataPort = await device.getFPGADataPortAXI();
                    return {
                        axiLite: new AxiLiteOverAxiDataPort(axiDataPort),
                        axiStream: new AxiStreamOverAxiDataPort(axiDataPort),
                    };
                }
                default:
                    throw new Error(`Unsupported device configuration: ${DeviceConfiguration[config]}`);
            }
        };

        const targetDeviceSerialNumber = (window.FrontPanelEnv.targetDeviceSerialNumbers.length > 0) ? window.FrontPanelEnv.targetDeviceSerialNumbers[0] : "";
        const deviceManager = window.FrontPanelAPI.deviceManager;

        let device: IDevice;
        let deviceInfo: IDeviceInfo;

        let deviceDisconnectedSubscription: IEventSubscription;

        DeviceWorkQueue.post(async () => {
            console.log(`Opening Device '${targetDeviceSerialNumber}'...`);

            await deviceManager.startMonitoring();

            // Step 1: Open the Device
            try {
                device = await deviceManager.openDevice(targetDeviceSerialNumber);

                deviceInfo = await device.getDeviceInfo();

                console.log(`Opened Device Product: ${deviceInfo.productName} SerialNumber: ${deviceInfo.serialNumber}`);
            }
            catch(error) {
                console.error(`Failed to open Device ${targetDeviceSerialNumber}: \n${error}`);

                setCameraSystem(undefined);
                setError({
                    title: "Failed to Open Target Device",
                    description: `Unable to open device with serial number ${targetDeviceSerialNumber}`,
                    details: `${error}`,
                    solution: "Verify that the device is properly connected and restart the application."
                });
                return;
            }

            if(device) {
                // Step 2: Query peripherals and determine configuration
                let config: DeviceConfiguration | null = null;
                let cameraSyzygyPort = "";

                try {
                    const deviceSettings = await device.getDeviceSettings();

                    let syzygyPortConfig: { syzygyKey: string; syzygyPortLabel: string } | undefined;

                    // For the XEM8320-AU25P, the camera peripheral is connected to SYZYGY Port A
                    // For the SZG-HUB1450-AU10P, the camera peripheral is connected to SYZYGY Port A
                    if (deviceInfo.productName === "XEM8320-AU25P") {
                        syzygyPortConfig = { syzygyKey: "SYZYGY0_PRODUCT_MODEL", syzygyPortLabel: "A" };
                    }
                    else if (deviceInfo.productName === "SZG-HUB1450-AU10P") {
                        syzygyPortConfig = { syzygyKey: "SYZYGY0_PRODUCT_MODEL", syzygyPortLabel: "A" };
                    }

                    const isGen3 = deviceInfo.interfaceType === DeviceInterfaceType.GEN3;

                    if (isGen3) {
                        console.log(`Device ${deviceInfo.serialNumber} is GEN3`);
                    }

                    let cameraPeripheral: CameraProductModel | undefined;

                    if (syzygyPortConfig) {
                        cameraSyzygyPort = syzygyPortConfig.syzygyPortLabel;
                        const cameraPeripheralModel = await deviceSettings?.getValue(syzygyPortConfig.syzygyKey);

                        console.log(`Detected on SYZYGY Port ${syzygyPortConfig.syzygyPortLabel}: ${cameraPeripheralModel || "<empty>"}`);

                        cameraPeripheral = productModelToCameraModel(cameraPeripheralModel?.toString());

                        if(cameraPeripheralModel && !cameraPeripheral) {
                            console.warn(`Unsupported Camera ${cameraPeripheralModel} detected on SYZYGY Port ${syzygyPortConfig.syzygyPortLabel}`);
                        }
                    }
                    else {
                        console.warn(`Unexpected Device Product Name ${deviceInfo.productName}. Unable to query for connected peripherals.`);
                    }

                    config = determineConfiguration(cameraPeripheral, isGen3);
                }
                catch(error) {
                    console.error(`Failed to query peripherals on Device ${deviceInfo.serialNumber}: \n${error}`);

                    device?.close();
                    setCameraSystem(undefined);
                    setError({
                        title: "Failed to Query Peripherals on Device",
                        description: `Unable to query peripherals on ${deviceInfo.productName} with serial number ${deviceInfo.serialNumber}`,
                        details: `${error}`,
                        solution: "Verify that the device is properly connected and restart the application."
                    });
                    return;
                }

                // Step 3: Handle unsupported configuration
                if (config === null) {
                    console.error(`Failed to find Camera connected to SYZYGY Port ${cameraSyzygyPort} of Device ${deviceInfo.serialNumber}`);

                    device?.close();
                    setCameraSystem(undefined);
                    setError({
                        title: "Failed to find Camera peripheral",
                        description: `Unable to find supported Camera connected on SYZYGY Port ${cameraSyzygyPort} of ${deviceInfo.productName} with serial number ${deviceInfo.serialNumber}`,
                        details: "",
                        solution: `Verify that the SZG-CAMERA is connected on SYZYGY Port ${cameraSyzygyPort} of the device and restart the application.`
                    });
                    return;
                }

                // Step 4: Initialize device and create driver
                try {
                    
                    await initializeDevice(device, config);
                    const { axiLite, axiStream } = await loadAxiInterfaces(device, config);
                    const mode = CAMERA_MODE_MAP[config];

                    // Create IP core drivers (shared between sequencer and UI)
                    const isp: IISP = mode === "tpg"
                        ? new NullISPDriver()
                        : new ISPDriver(axiLite);
                    const tpg = new TPGDriver(axiLite);

                    // Create camera control (owns I2C internally)
                    let control: ICameraControl;
                    switch (mode) {
                        case "tpg":
                            control = new TPGCameraControl();
                            break;
                        case "pcam":
                            control = new PCAMCameraControl(axiLite);
                            break;
                        case "szgcam":
                            control = new SYZYGYCameraControl(axiLite);
                            break;
                    }

                    const sequencer = new CapturePipelineSequencer(axiLite, axiStream, mode, isp, tpg, control, DeviceWorkQueue);

                    setCameraSystem({ sequencer, isp, tpg, control, mode });
                }
                catch(error) {
                    const isTPG = config === DeviceConfiguration.HUB1450_TPG || config === DeviceConfiguration.XEM8320_TPG;
                    console.error(`Failed to initialize ${isTPG ? "TPG mode on " : ""}Device ${deviceInfo.serialNumber}: \n${error}`);

                    device?.close();
                    setCameraSystem(undefined);
                    setError({
                        title: isTPG ? "Failed to Initialize TPG Mode" : "Failed to Initialize Device",
                        description: isTPG
                            ? `Unable to initialize TPG mode on ${deviceInfo.productName} with serial number ${deviceInfo.serialNumber}`
                            : `Unable to initialize ${deviceInfo.productName} with serial number ${deviceInfo.serialNumber}`,
                        details: `${error}`,
                        solution: "Verify that the device is properly connected and restart the application."
                    });
                    return;
                }

                // Step 5: Register for disconnect events (only on successful init)
                deviceDisconnectedSubscription = deviceManager.deviceDisconnectedEvent.subscribeAsync(
                    async (sender: IDeviceManager, serialNumber: string) => {
                        console.info("Device Disconnected: " + serialNumber);
                        if(serialNumber === deviceInfo.serialNumber) {
                            setCameraSystem(undefined);
                            setError({
                                title: "Target Device Disconnected",
                                description: `${deviceInfo.productName} with serial number ${deviceInfo.serialNumber} was disconnected`,
                                details: "",
                                solution: "Connect the target device and restart the application."
                            });
                        }
                    }
                );
            }
        });

        return () => {
            console.log(`App Use Effect::Cleanup`)

            setCameraSystem(undefined);

            deviceDisconnectedSubscription?.cancel();

            DeviceWorkQueue.post(async () => {
                await deviceManager.stopMonitoring();

                console.log(`Closing Device...`);
                device?.close();
            });
        };
    }, []);

    // Event Handlers
    const onExitButtonClick = () => {
        window.close();
    }

    if (cameraSystem !== undefined) {
        return (
            <div className="App">
                <CameraView
                    name="Camera"
                    sequencer={cameraSystem.sequencer}
                    isp={cameraSystem.isp}
                    tpg={cameraSystem.tpg}
                    cameraControl={cameraSystem.control}
                    workQueue={DeviceWorkQueue}
                    cameraMode={cameraSystem.mode}
                />
            </div>
        );
    } else {
        const isInitializing = (cameraSystem === undefined) && (error === undefined);
        const showFPGAConfigurationStatus = (progress !== undefined);

        const formatProgressMessage = () => {
            if (progress !== undefined) {
                return `Configured ${progress.completed} of ${progress.total} bytes`;
            }
            return null;
        };

        const progressPercent = (progress !== undefined && progress.total > 0)
            ? Math.round((progress.completed / progress.total) * 100)
            : 0;

        return (
            <div className="AppLogo">
                <img src={FrontPanelLogo} />
                {isInitializing ?
                    <Dialog.Root open={showFPGAConfigurationStatus}>
                        <Dialog.Portal>
                            <Dialog.Overlay className="DialogOverlay" />
                            <Dialog.Content className="DialogContent">
                                <Dialog.Title className="DialogTitle">Configuring FPGA</Dialog.Title>
                                <div className="DialogBody">
                                    <Progress.Root className="ProgressBarRoot" value={progressPercent} max={100}>
                                        <Progress.Indicator
                                            className="ProgressBarIndicator"
                                            style={{ width: `${progressPercent}%` }}
                                        />
                                    </Progress.Root>
                                    <div className="ProgressText">{formatProgressMessage()}</div>
                                </div>
                            </Dialog.Content>
                        </Dialog.Portal>
                    </Dialog.Root>
                :
                    <AlertDialog.Root open={(error !== undefined)}>
                        <AlertDialog.Portal>
                            <AlertDialog.Content className="AlertDialogContent">
                                <AlertDialog.Title className="AlertDialogTitle">{error?.title}</AlertDialog.Title>
                                <div style={{ height: "1px", alignSelf: "stretch", background: "#3B4046", borderRadius: "4px" }}/>
                                <AlertDialog.Description className="AlertDialogDescription">
                                    {error?.description}
                                </AlertDialog.Description>
                                <div className="AlertDialogInstructions">
                                    {error?.solution}
                                </div>
                                <div className="AlertDialogActionPanel">
                                    <AlertDialog.Action asChild>
                                        <button
                                            className="okButton"
                                            onClick={(event) => onExitButtonClick()}>Exit Application</button>
                                    </AlertDialog.Action>
                                </div>
                            </AlertDialog.Content>
                        </AlertDialog.Portal>
                    </AlertDialog.Root>
                }
            </div>
        );
    }
}

export default App;
