/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// IMX219CameraControl.ts - Sony IMX219 (Raspberry Pi Camera Module 2) control.
//
// Used with the XEM8310-AU25P "IMX219" bitfile (camera_imx219.bit): CSI-2 RX (2 lanes, RAW10)
// -> ISP -> TPG (passthrough) -> VDMA, like the OV5640 design. The AXI IIC IP is shared with the
// other sensors (I2CController). TypeScript port of Software/Cxx/src/IMX219CameraControl.h.
//
// Register values follow the Linux kernel driver drivers/media/i2c/imx219.c (2-lane mode,
// 24 MHz INCK, 1920x1080 cropped mode) and the IMX219PQH5-C datasheet:
//   - pixel rate 182.4 Mpix/s (VT PLL 456 MHz), 912 Mb/s per lane (OP PLL 912 MHz);
//   - 1920x1080: centred crop (680,692)-(2599,1771) of the active array, no binning;
//   - LINE_LENGTH_A = 3448, FRAME_LENGTH_A = 1763 -> 30 fps;
//   - default orientation (no flip) gives an RGGB Bayer pattern.
//
// The module has its own 24 MHz oscillator (INCK is not on the 15-pin FFC). GPIO0 (FFC pin 11,
// driven by pcam_power_en = AXI IIC GPO, default 1) must be high to enable the sensor before I2C
// access. VIO2 of the XEM8310 must be 1.8 V.

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
// IMX219 device constants
// ============================================================

const IMX219_DEFAULT_SIZE: MatrixDimensions = { columnCount: 1920, rowCount: 1080 };

/** 7-bit I2C address (datasheet fig. 10: 0010000x). */
const IMX219_I2C_ADDR_7BIT = 0x10;
const IMX219_CHIP_ID = 0x0219;

const PIXEL_RATE_HZ = 182400000;
const LINE_LENGTH_PCK = 3448; // 0x0d78 (minimum, kernel default)
const FRAME_LENGTH_LINES = 1763; // 30 fps at 1920x1080
const EXPOSURE_MIN_LINES = 4;
const DEFAULT_EXPOSURE_MS = 10;

// Register addresses
const REG_CHIP_ID = 0x0000;
const REG_FRM_CNT = 0x0018;
const REG_MODE_SELECT = 0x0100;
const REG_CSI_LANE_MODE = 0x0114;
const REG_DPHY_CTRL = 0x0128;
const REG_EXCK_FREQ = 0x012a;
const REG_ANALOG_GAIN = 0x0157;
const REG_DIGITAL_GAIN = 0x0158;
const REG_COARSE_INTEGRATION_TIME = 0x015a;
const REG_FRM_LENGTH_A = 0x0160;
const REG_LINE_LENGTH_A = 0x0162;
const REG_X_ADD_STA_A = 0x0164;
const REG_X_ADD_END_A = 0x0166;
const REG_Y_ADD_STA_A = 0x0168;
const REG_Y_ADD_END_A = 0x016a;
const REG_X_OUTPUT_SIZE = 0x016c;
const REG_Y_OUTPUT_SIZE = 0x016e;
const REG_X_ODD_INC_A = 0x0170;
const REG_Y_ODD_INC_A = 0x0171;
const REG_ORIENTATION = 0x0172;
const REG_BINNING_MODE_H = 0x0174;
const REG_BINNING_MODE_V = 0x0175;
const REG_CSI_DATA_FORMAT_A = 0x018c;
const REG_VTPXCK_DIV = 0x0301;
const REG_VTSYCK_DIV = 0x0303;
const REG_PREPLLCK_VT_DIV = 0x0304;
const REG_PREPLLCK_OP_DIV = 0x0305;
const REG_PLL_VT_MPY = 0x0306;
const REG_OPPXCK_DIV = 0x0309;
const REG_OPSYCK_DIV = 0x030b;
const REG_PLL_OP_MPY = 0x030c;
const REG_TP_WINDOW_WIDTH = 0x0624;
const REG_TP_WINDOW_HEIGHT = 0x0626;

/** Undocumented registers (kernel driver), written after the manufacturer-register access sequence. */
const UNDOCUMENTED_REGS: ReadonlyArray<readonly [number, number]> = [
    [0x455e, 0x00],
    [0x471e, 0x4b],
    [0x4767, 0x0f],
    [0x4750, 0x14],
    [0x4540, 0x00],
    [0x47b4, 0x14],
    [0x4713, 0x30],
    [0x478b, 0x10],
    [0x478f, 0x10],
    [0x4793, 0x10],
    [0x4797, 0x0e],
    [0x479b, 0x0e]
];

/**
 * Analogue gain code (register 0x0157) for a gain multiplier: gain = 256 / (256 - code),
 * code 0..232 (1.0x .. 10.67x). A higher gain keeps a short exposure usable in dim light.
 */
export function imx219AnalogGainCode(multiplier: number): number {
    if (!(multiplier > 1)) return 0;
    const code = Math.round(256 - 256 / multiplier);
    return Math.max(0, Math.min(232, code));
}

// ============================================================
// IMX219CameraControl
// ============================================================

export class IMX219CameraControl implements ICameraControl {
    private readonly _i2c: I2CController;
    private readonly _dev7bit = IMX219_I2C_ADDR_7BIT;
    private readonly _analogGainCode: number;

    private _size: IMatrixDimensions;
    private _skips: IMatrixDimensions;
    private _frameDimensions: IMatrixDimensions;
    private _exposure: CameraExposure = 0;
    private _streaming = false;

    /**
     * @param axiLite     AXI-Lite interface (the AXI IIC IP is reached through it).
     * @param analogGain  Optional analogue gain multiplier (1.0 .. 10.67), default 1.0.
     */
    constructor(axiLite: IAxiLite, analogGain: number = 1.0) {
        this._i2c = new I2CController(axiLite);
        this._size = IMX219_DEFAULT_SIZE;
        this._skips = { columnCount: 0, rowCount: 0 };
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
        this._analogGainCode = imx219AnalogGainCode(analogGain);
    }

    // ================================================================
    // ICameraControl accessors
    // ================================================================

    public get defaultSize(): MatrixDimensions {
        return IMX219_DEFAULT_SIZE;
    }

    public get supportedSkips(): MatrixDimensions[] {
        return [{ rowCount: 0, columnCount: 0 }];
    }

    public get supportedTestModes(): TestMode[] {
        return TPG_SUPPORTED_TEST_MODES;
    }

    /** Only the cropped 1920x1080 mode is supported for now (the ISP is built for 1920x1080). */
    public get supportedFrameConfigurations(): FrameConfiguration[] {
        return [{ dimensions: IMX219_DEFAULT_SIZE, skips: { rowCount: 0, columnCount: 0 } }];
    }

    public get frameDimensions(): IMatrixDimensions {
        return this._frameDimensions;
    }

    public get exposure(): CameraExposure {
        return this._exposure;
    }

    // ================================================================
    // ICameraControl operations
    // ================================================================

    /**
     * Full sensor setup; leaves the sensor in software standby (see afterPipelineStart()).
     * Throws if the sensor does not answer with chip id 0x0219.
     */
    public async initialize(): Promise<void> {
        await this._i2c.initialize();

        const id = await this._i2c.read16(this._dev7bit, REG_CHIP_ID);
        if (id !== IMX219_CHIP_ID) {
            throw new Error(
                `IMX219 not found on I2C 0x10 (chip id 0x${id.toString(16).padStart(4, "0")}, ` +
                    `expected 0x0219). Check the camera cable, GPIO0 and VIO2 = 1.8 V.`
            );
        }

        await this.w8(REG_MODE_SELECT, 0x00); // software standby

        // Access to the manufacturer-specific registers (datasheet table 11).
        await this.w8(0x30eb, 0x05);
        await this.w8(0x30eb, 0x0c);
        await this.w8(0x300a, 0xff);
        await this.w8(0x300b, 0xff);
        await this.w8(0x30eb, 0x05);
        await this.w8(0x30eb, 0x09);

        for (const [reg, value] of UNDOCUMENTED_REGS) {
            await this.w8(reg, value);
        }

        await this.w8(REG_X_ODD_INC_A, 1);
        await this.w8(REG_Y_ODD_INC_A, 1);

        // Output setup: automatic D-PHY timing, INCK = 24 MHz.
        await this.w8(REG_DPHY_CTRL, 0x00);
        await this.w16(REG_EXCK_FREQ, 24 * 256);

        // 2-lane PLL setup: VT 456 MHz (182.4 Mpix/s), OP 912 MHz (912 Mb/s per lane).
        await this.w8(REG_VTPXCK_DIV, 5);
        await this.w8(REG_VTSYCK_DIV, 1);
        await this.w8(REG_PREPLLCK_VT_DIV, 3);
        await this.w8(REG_PREPLLCK_OP_DIV, 3);
        await this.w16(REG_PLL_VT_MPY, 57);
        await this.w8(REG_OPSYCK_DIV, 1);
        await this.w16(REG_PLL_OP_MPY, 114);
        await this.w8(REG_CSI_LANE_MODE, 0x01); // 2 lanes

        await this.setupMode1080p();

        // Exposure / gain / orientation defaults.
        await this.w8(REG_ORIENTATION, 0x00); // no flip -> RGGB
        await this.w8(REG_ANALOG_GAIN, this._analogGainCode);
        await this.w16(REG_DIGITAL_GAIN, 0x0100);
        await this.setExposure(this._exposure > 0 ? this._exposure : DEFAULT_EXPOSURE_MS);

        // The sensor stays in standby here: it is started by afterPipelineStart(), once the CSI-2
        // receiver has been reset and is idle (the D-PHY must see LP-11 and then the start of
        // transmission after its reset).
        this._streaming = false;

        this._size = IMX219_DEFAULT_SIZE;
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);

        console.log("IMX219CameraControl::initialize() Complete");
    }

    /**
     * Exposure in milliseconds -> COARSE_INTEGRATION_TIME in line periods. The IMX219 has no AEC:
     * the value is a real integration time, limited to one frame period (about 33 ms at 30 fps).
     */
    public async setExposure(exposure: CameraExposure): Promise<void> {
        this._exposure = exposure;

        const lineUs = (LINE_LENGTH_PCK / PIXEL_RATE_HZ) * 1e6;
        let lines = Math.round((exposure * 1000) / lineUs);
        lines = Math.max(EXPOSURE_MIN_LINES, Math.min(FRAME_LENGTH_LINES - 4, lines));

        await this.w16(REG_COARSE_INTEGRATION_TIME, lines);
    }

    public async setSize(size: MatrixDimensions): Promise<void> {
        if (size.columnCount !== 1920 || size.rowCount !== 1080) {
            throw new Error(`Unsupported IMX219 resolution: ${size.columnCount}x${size.rowCount}`);
        }

        this._size = size;
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async setSkips(_skips: MatrixDimensions): Promise<void> {
        this._skips = { rowCount: 0, columnCount: 0 };
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async reinitializeI2C(): Promise<void> {
        await this._i2c.initialize();
    }

    /** Put the sensor back in standby before the receiver is reset, so that it restarts cleanly. */
    public async beforePipelineReset(): Promise<void> {
        if (this._streaming) {
            await this.w8(REG_MODE_SELECT, 0x00);
            this._streaming = false;
        }
    }

    /** Start streaming now that the pipeline (including the CSI-2 receiver) is up. */
    public async afterPipelineStart(): Promise<void> {
        if (!this._streaming) {
            await this.w8(REG_MODE_SELECT, 0x01);
            this._streaming = true;
        }
    }

    /**
     * Bring-up aid: sensor state on the console. FRM_CNT (0x0018) increments with every frame the
     * sensor produces (0xFF = none yet).
     */
    public async dumpDiagnostics(): Promise<void> {
        try {
            const d = this._dev7bit;
            const id = await this._i2c.read16(d, REG_CHIP_ID);
            const mode = await this._i2c.read8(d, REG_MODE_SELECT);
            const frames = await this._i2c.read8(d, REG_FRM_CNT);
            console.log(
                `[IMX219] id=0x${id.toString(16).padStart(4, "0")} mode_select=${mode} frm_cnt=${frames}`
            );
        } catch (error) {
            console.warn(`[IMX219] diagnostics failed: ${error}`);
        }
    }

    // ================================================================
    // Private helpers
    // ================================================================

    private w8(reg: number, data: number): Promise<void> {
        return this._i2c.write8(this._dev7bit, reg, data);
    }

    /** 16-bit register write, one 2-byte burst (CCI style, big endian). */
    private w16(reg: number, data: number): Promise<void> {
        return this._i2c.write16(this._dev7bit, reg, data);
    }

    /**
     * 1920x1080 30 fps, RAW10, no binning. The crop is centred in the 3296x2480 native array
     * (active area starts at (8,8)): left=688, top=700 -> start (680,692).
     */
    private async setupMode1080p(): Promise<void> {
        await this.w16(REG_X_ADD_STA_A, 680);
        await this.w16(REG_X_ADD_END_A, 680 + 1920 - 1);
        await this.w16(REG_Y_ADD_STA_A, 692);
        await this.w16(REG_Y_ADD_END_A, 692 + 1080 - 1);
        await this.w8(REG_BINNING_MODE_H, 0x00);
        await this.w8(REG_BINNING_MODE_V, 0x00);
        await this.w16(REG_X_OUTPUT_SIZE, 1920);
        await this.w16(REG_Y_OUTPUT_SIZE, 1080);
        await this.w16(REG_TP_WINDOW_WIDTH, 1920);
        await this.w16(REG_TP_WINDOW_HEIGHT, 1080);
        await this.w16(REG_CSI_DATA_FORMAT_A, (10 << 8) | 10); // RAW10
        await this.w8(REG_OPPXCK_DIV, 10);
        await this.w16(REG_LINE_LENGTH_A, LINE_LENGTH_PCK);
        await this.w16(REG_FRM_LENGTH_A, FRAME_LENGTH_LINES);
    }
}
