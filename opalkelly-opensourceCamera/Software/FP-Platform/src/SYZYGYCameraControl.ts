/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import { ICameraControl } from "./ICameraControl";
import { I2CController } from "./I2CController";
import { IAxiLite } from "./IAxi";
import {
    CameraExposure,
    FrameConfiguration,
    IMatrixDimensions,
    MatrixDimensions,
    TestMode,
    calculateFrameDimensions
} from "./CameraTypes";

import { TPG_SUPPORTED_TEST_MODES } from "./TPGPatterns";

// ============================================================
// AR0330 Device Constants
// ============================================================

const AR0330_DEFAULT_SIZE: MatrixDimensions = { columnCount: 2304, rowCount: 1296 };

const DEVICE_ADDRESS_AR0330 = 0x10;  // 7-bit, SADDR=low (GPO default)

// ============================================================
// AR0330 Register Addresses
// ============================================================

const AR0330_REG_Y_ADDR_END = 0x3006;
const AR0330_REG_X_ADDR_END = 0x3008;
const AR0330_REG_LINE_LENGTH_PCK = 0x300c;
const AR0330_REG_COARSE_INTEGRATION_TIME = 0x3012;
const AR0330_REG_MODE_SELECT = 0x301c;
const AR0330_REG_VT_PIX_CLK_DIV = 0x302a;
const AR0330_REG_PRE_PLL_CLK_DIV = 0x302e;
const AR0330_REG_PLL_MULTIPLIER = 0x3030;
const AR0330_REG_OP_PIX_CLK_DIV = 0x3036;
const AR0330_REG_OP_SYS_CLK_DIV = 0x3038;
const AR0330_REG_ANALOG_GAIN = 0x3060;
const AR0330_REG_SMIA_TEST = 0x3064;
const AR0330_REG_DATAPATH_SELECT = 0x306e;
const AR0330_REG_TEST_PATTERN_MODE = 0x3070;
const AR0330_REG_X_ODD_INC = 0x30a2;
const AR0330_REG_Y_ODD_INC = 0x30a6;
const AR0330_REG_DATA_FORMAT_BITS = 0x31ac;
const AR0330_REG_HISPI_CONTROL_STATUS = 0x31c6;
const AR0330_REG_COMPRESSION = 0x31d0;

// ============================================================
// SYZYGYCameraControl
// ============================================================

export class SYZYGYCameraControl implements ICameraControl {
    private readonly _i2c: I2CController;

    private _size: IMatrixDimensions;
    private _skips: IMatrixDimensions;
    private _frameDimensions: IMatrixDimensions;
    private _exposure: CameraExposure = 0;

    constructor(axiLite: IAxiLite) {
        this._i2c = new I2CController(axiLite);
        this._size = AR0330_DEFAULT_SIZE;
        this._skips = { columnCount: 0, rowCount: 0 };
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    // Accessors

    public get defaultSize(): MatrixDimensions {
        return AR0330_DEFAULT_SIZE;
    }

    public get supportedSkips(): MatrixDimensions[] {
        return [
            { rowCount: 0, columnCount: 0 },
            { rowCount: 1, columnCount: 1 },
            { rowCount: 2, columnCount: 2 }
        ];
    }

    public get supportedTestModes(): TestMode[] {
        return TPG_SUPPORTED_TEST_MODES;
    }

    public get supportedFrameConfigurations(): FrameConfiguration[] {
        const retval: FrameConfiguration[] = [];

        this.supportedSkips.forEach((skips) => {
            const frameDimensions: MatrixDimensions = calculateFrameDimensions(
                AR0330_DEFAULT_SIZE,
                skips
            );

            retval.push({ dimensions: frameDimensions, skips: skips });
        });

        return retval;
    }

    public get frameDimensions(): IMatrixDimensions {
        return this._frameDimensions;
    }

    public get exposure(): CameraExposure {
        return this._exposure;
    }

    // Operations

    // Performs full reset of SZG-CAMERA device (sensor setup only, no pipeline reset)
    public async initialize(): Promise<void> {
        await this._i2c.initialize();

        // Setup image sensor registers BEFORE pipeline reset so that the camera
        // sensor is streaming and vid_clk is alive when reconfigurePipeline()
        // resets the AXI-Stream (PHY). Without vid_clk, the TPG/ISP/Histogram
        // IPs on smartconnect_2 cannot be configured.
        await this.setupOptimizedRegisterSet();

        console.log("SYZYGYCameraControl::initialize() Complete");
    }

    /**
     * Set the exposure (coarse integration time) on the AR0330.
     *
     * Converts the desired exposure in milliseconds to row periods and writes
     * the COARSE_INTEGRATION_TIME register directly. Each row period equals
     * LINE_LENGTH_PCK * pixel clock period (34 ns). The sensor does not have
     * an auto-exposure engine — the host controls exposure time directly.
     *
     * If the value exceeds FRAME_LENGTH_LINES, the sensor extends the frame
     * period to accommodate, reducing the effective frame rate.
     *
     * @param exposure  Exposure duration in milliseconds.
     */
    public async setExposure(exposure: CameraExposure): Promise<void> {
        this._exposure = exposure;
        const pixClkNs = 34;      // Pixel Clock Rate in Nanoseconds
        const exposureMs: number = exposure;

        // Determine the number of clock periods per row
        const lineLengthPck: number = await this._i2c.read16(
            DEVICE_ADDRESS_AR0330,
            AR0330_REG_LINE_LENGTH_PCK
        );

        // Convert milliseconds to row periods (coarse integration time)
        const exposureLlpck: number = Math.floor(
            (exposureMs * 1000000) / (lineLengthPck * pixClkNs)
        );

        console.log(`Clock Periods per Row: ${lineLengthPck}`);
        console.log(`Exposure: ${exposureMs} ms => Coarse Integration Time: ${exposureLlpck}`);

        // Set the number of row periods between row reset and read
        await this._i2c.write16(
            DEVICE_ADDRESS_AR0330,
            AR0330_REG_COARSE_INTEGRATION_TIME,
            exposureLlpck & 0xffff
        );
    }

    public async setSize(_size: MatrixDimensions): Promise<void> {
        // AR0330 always captures the full sensor area; output resolution is controlled by setSkips.
        const sensorSize = AR0330_DEFAULT_SIZE;
        await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_X_ADDR_END, sensorSize.columnCount + 6 - 1);
        await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_Y_ADDR_END, sensorSize.rowCount + 124 - 1);

        this._size = sensorSize;
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async setSkips(skips: MatrixDimensions): Promise<void> {
        if (skips.columnCount === 0) {
            await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_X_ODD_INC, 1);
        } else if (skips.columnCount === 1) {
            await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_X_ODD_INC, 3);
        } else if (skips.columnCount === 2) {
            await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_X_ODD_INC, 5);
        } else {
            throw new Error(`Unsupported column skip value ${skips.columnCount}`);
        }

        if (skips.rowCount === 0) {
            await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_Y_ODD_INC, 1);
        } else if (skips.rowCount === 1) {
            await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_Y_ODD_INC, 3);
        } else if (skips.rowCount === 2) {
            await this._i2c.write16(DEVICE_ADDRESS_AR0330, AR0330_REG_Y_ODD_INC, 5);
        } else {
            throw new Error(`Unsupported row skip value ${skips.rowCount}`);
        }

        this._skips = skips;
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async reinitializeI2C(): Promise<void> {
        await this._i2c.initialize();
    }


    //
    private async setupOptimizedRegisterSet(): Promise<void> {
        const addr = DEVICE_ADDRESS_AR0330;

        // Setup sensor for 1080p 30fps
        await this._i2c.write16(addr, AR0330_REG_HISPI_CONTROL_STATUS, 0x8400); // hispi_control setting
        await this._i2c.write16(addr, AR0330_REG_SMIA_TEST, 0x1802); // Disable embedded Data
        await this._i2c.write16(addr, AR0330_REG_DATA_FORMAT_BITS, 0x0a0a); // Data Width
        await this._i2c.write16(addr, AR0330_REG_COMPRESSION, 0x0000); // Disable compression
        await this._i2c.write16(addr, AR0330_REG_DATAPATH_SELECT, 0x0210); // Datapath select
        await this._i2c.write16(addr, AR0330_REG_VT_PIX_CLK_DIV, 0x0005);
        await this._i2c.write16(addr, AR0330_REG_PRE_PLL_CLK_DIV, 0x0002);
        await this._i2c.write16(addr, AR0330_REG_PLL_MULTIPLIER, 0x0028);
        await this._i2c.write16(addr, AR0330_REG_OP_SYS_CLK_DIV, 0x0001);
        await this._i2c.write16(addr, AR0330_REG_OP_PIX_CLK_DIV, 0x000a); // op_pix_clk_div(data width)
        await this._i2c.write16(addr, AR0330_REG_COARSE_INTEGRATION_TIME, 0x0400); // Increase exposure 400 for sensor + lens, 20 for bare sensor
        await this._i2c.write16(addr, AR0330_REG_ANALOG_GAIN, 0x0018); // Set gain to ISO 400

        await this._i2c.write16(addr, AR0330_REG_TEST_PATTERN_MODE, 0x0000); // Disable test pattern

        await this._i2c.write16(addr, AR0330_REG_MODE_SELECT, 0x0100); // Enable streaming
    }
}
