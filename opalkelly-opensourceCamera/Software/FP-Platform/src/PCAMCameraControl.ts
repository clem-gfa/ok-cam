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
// OV5640 Device Constants
// ============================================================

const OV5640_DEFAULT_SIZE: MatrixDimensions = { columnCount: 1920, rowCount: 1080 };

const OV5640_I2C_ADDR_7BIT = 0x3c;

const OV5640_RESOLUTIONS: MatrixDimensions[] = [
    { columnCount: 1920, rowCount: 1080 },
    { columnCount: 1280, rowCount: 720 }
];

// ============================================================
// OV5640 Register Addresses
// ============================================================

const OV5640_REG_SYSTEM_CTRL = 0x3008; // [7]=SW reset, [6]=SW power down
const OV5640_REG_AEC_CTRL0F = 0x3a0f;  // Stable Range High Limit (enter) — WPT
const OV5640_REG_AEC_CTRL10 = 0x3a10;  // Stable Range Low Limit (enter) — BPT
const OV5640_REG_AEC_CTRL1B = 0x3a1b;  // Stable Range High Limit (go out) — WPT2
const OV5640_REG_AEC_CTRL1E = 0x3a1e;  // Stable Range Low Limit (go out) — BPT2

// ============================================================
// OV5640 I2C Write Helper Type
// ============================================================

/** Callback type for 8-bit I2C writes (16-bit register address, 8-bit data). */
type I2CWrite8 = (dev7bit: number, registerAddress: number, data: number) => Promise<void>;

// ============================================================
// OV5640 Sensor Initialization Sequences
// ============================================================

/**
 * Base sensor initialization. Translated from pcam_init() in pcam.py.
 * Sensor remains powered down after this call.
 */
async function ov5640Init(w8: I2CWrite8, dev: number): Promise<void> {
    // Initial power-down configuration (Digilent reference)
    await w8(dev, 0x3008, 0x42);
    await w8(dev, 0x3103, 0x03);
    await w8(dev, 0x3017, 0x00);
    await w8(dev, 0x3018, 0x00);
    await w8(dev, 0x3034, 0x18);
    await w8(dev, 0x3035, 0x11);
    await w8(dev, 0x3036, 0x38);
    await w8(dev, 0x3037, 0x11);
    await w8(dev, 0x3108, 0x01);
    await w8(dev, 0x303d, 0x10);
    await w8(dev, 0x303b, 0x19);
    await w8(dev, 0x3630, 0x2e);
    await w8(dev, 0x3631, 0x0e);
    await w8(dev, 0x3632, 0xe2);
    await w8(dev, 0x3633, 0x23);
    await w8(dev, 0x3621, 0xe0);
    await w8(dev, 0x3704, 0xa0);
    await w8(dev, 0x3703, 0x5a);
    await w8(dev, 0x3715, 0x78);
    await w8(dev, 0x3717, 0x01);
    await w8(dev, 0x370b, 0x60);
    await w8(dev, 0x3705, 0x1a);
    await w8(dev, 0x3905, 0x02);
    await w8(dev, 0x3906, 0x10);
    await w8(dev, 0x3901, 0x0a);
    await w8(dev, 0x3731, 0x02);
    await w8(dev, 0x3600, 0x37);
    await w8(dev, 0x3601, 0x33);
    await w8(dev, 0x302d, 0x60);
    await w8(dev, 0x3620, 0x52);
    await w8(dev, 0x371b, 0x20);
    await w8(dev, 0x471c, 0x50);
    await w8(dev, 0x3a13, 0x43);
    await w8(dev, 0x3a18, 0x00);
    await w8(dev, 0x3a19, 0xf8);
    await w8(dev, 0x3635, 0x13);
    await w8(dev, 0x3636, 0x06);
    await w8(dev, 0x3634, 0x44);
    await w8(dev, 0x3622, 0x01);
    await w8(dev, 0x3c01, 0x34);
    await w8(dev, 0x3c04, 0x28);
    await w8(dev, 0x3c05, 0x98);
    await w8(dev, 0x3c06, 0x00);
    await w8(dev, 0x3c07, 0x08);
    await w8(dev, 0x3c08, 0x00);
    await w8(dev, 0x3c09, 0x1c);
    await w8(dev, 0x3c0a, 0x9c);
    await w8(dev, 0x3c0b, 0x40);
    await w8(dev, 0x503d, 0x00);
    await w8(dev, 0x3820, 0x46);
    await w8(dev, 0x300e, 0x45);
    await w8(dev, 0x4800, 0x14);
    await w8(dev, 0x302e, 0x08);
    await w8(dev, 0x4300, 0x6f);
    await w8(dev, 0x501f, 0x01);
    await w8(dev, 0x4713, 0x03);
    await w8(dev, 0x4407, 0x04);
    await w8(dev, 0x440e, 0x00);
    await w8(dev, 0x460b, 0x35);
    await w8(dev, 0x460c, 0x20);
    await w8(dev, 0x3824, 0x01);
    await w8(dev, 0x5000, 0x07);
    await w8(dev, 0x5001, 0x03);
    // Sensor remains in power down
}

/**
 * Initialize AWB parameters (does not enable AWB).
 * Translated from pcam_awb_init() in pcam.py.
 */
async function ov5640AwbInit(w8: I2CWrite8, dev: number): Promise<void> {
    await w8(dev, 0x3008, 0x42); // Power down

    // Advanced AWB (Digilent OV5640 reference)
    await w8(dev, 0x3406, 0x00);
    await w8(dev, 0x5192, 0x04);
    await w8(dev, 0x5191, 0xf8);
    await w8(dev, 0x518d, 0x26);
    await w8(dev, 0x518f, 0x42);
    await w8(dev, 0x518e, 0x2b);
    await w8(dev, 0x5190, 0x42);
    await w8(dev, 0x518b, 0xd0);
    await w8(dev, 0x518c, 0xbd);
    await w8(dev, 0x5187, 0x18);
    await w8(dev, 0x5188, 0x18);
    await w8(dev, 0x5189, 0x56);
    await w8(dev, 0x518a, 0x5c);
    await w8(dev, 0x5186, 0x1c);
    await w8(dev, 0x5181, 0x50);
    await w8(dev, 0x5184, 0x20);
    await w8(dev, 0x5182, 0x11);
    await w8(dev, 0x5183, 0x00);

    await w8(dev, 0x3008, 0x02); // Power on
}

/**
 * Configure OV5640 for 1920x1080 30fps.
 * Translated from pcam_setup_init_mode() in pcam.py.
 */
async function ov5640Setup1080p(w8: I2CWrite8, dev: number): Promise<void> {
    await w8(dev, 0x3008, 0x42); // Power down

    // PLL configuration
    await w8(dev, 0x3035, 0x21);
    await w8(dev, 0x3036, 0x69);
    await w8(dev, 0x3037, 0x05);
    await w8(dev, 0x3108, 0x11);
    await w8(dev, 0x3034, 0x1a);

    // Crop window: (336, 426) to (2287, 1529)
    await w8(dev, 0x3800, (336 >> 8) & 0x0f);
    await w8(dev, 0x3801, 336 & 0xff);
    await w8(dev, 0x3802, (426 >> 8) & 0x07);
    await w8(dev, 0x3803, 426 & 0xff);
    await w8(dev, 0x3804, (2287 >> 8) & 0x0f);
    await w8(dev, 0x3805, 2287 & 0xff);
    await w8(dev, 0x3806, (1529 >> 8) & 0x07);
    await w8(dev, 0x3807, 1529 & 0xff);

    // Output offset: (16, 12)
    await w8(dev, 0x3810, (16 >> 8) & 0x0f);
    await w8(dev, 0x3811, 16 & 0xff);
    await w8(dev, 0x3812, (12 >> 8) & 0x07);
    await w8(dev, 0x3813, 12 & 0xff);

    // Output size: 1920x1080
    await w8(dev, 0x3808, (1920 >> 8) & 0x0f);
    await w8(dev, 0x3809, 1920 & 0xff);
    await w8(dev, 0x380a, (1080 >> 8) & 0x7f);
    await w8(dev, 0x380b, 1080 & 0xff);

    // Timing: HTS=2500, VTS=1120
    await w8(dev, 0x380c, (2500 >> 8) & 0x1f);
    await w8(dev, 0x380d, 2500 & 0xff);
    await w8(dev, 0x380e, (1120 >> 8) & 0xff);
    await w8(dev, 0x380f, 1120 & 0xff);

    // No binning, no mirror
    await w8(dev, 0x3814, 0x11);
    await w8(dev, 0x3815, 0x11);
    await w8(dev, 0x3821, 0x00);

    // MIPI pclk period, analog path, output format
    await w8(dev, 0x4837, 24);
    await w8(dev, 0x3618, 0x00);
    await w8(dev, 0x3612, 0x59);
    await w8(dev, 0x3708, 0x64);
    await w8(dev, 0x3709, 0x52);
    await w8(dev, 0x370c, 0x03);
    await w8(dev, 0x4300, 0x00);
    await w8(dev, 0x501f, 0x03); // RGB output via ISP

    await w8(dev, 0x3008, 0x02); // Power on
}

/**
 * Configure OV5640 for 1280x720 60fps.
 * Translated from pcam_setup_init_mode_720p() in pcam.py.
 */
async function ov5640Setup720p(w8: I2CWrite8, dev: number): Promise<void> {
    await w8(dev, 0x3008, 0x42); // Power down

    // PLL configuration
    await w8(dev, 0x3035, 0x21);
    await w8(dev, 0x3036, 0x46);
    await w8(dev, 0x3037, 0x05);
    await w8(dev, 0x3108, 0x11);
    await w8(dev, 0x3034, 0x1a);

    // Crop window: (0, 8) to (2619, 1947)
    await w8(dev, 0x3800, (0 >> 8) & 0x0f);
    await w8(dev, 0x3801, 0 & 0xff);
    await w8(dev, 0x3802, (8 >> 8) & 0x07);
    await w8(dev, 0x3803, 8 & 0xff);
    await w8(dev, 0x3804, (2619 >> 8) & 0x0f);
    await w8(dev, 0x3805, 2619 & 0xff);
    await w8(dev, 0x3806, (1947 >> 8) & 0x07);
    await w8(dev, 0x3807, 1947 & 0xff);

    // Output offset: (0, 0)
    await w8(dev, 0x3810, (0 >> 8) & 0x0f);
    await w8(dev, 0x3811, 0 & 0xff);
    await w8(dev, 0x3812, (0 >> 8) & 0x07);
    await w8(dev, 0x3813, 0 & 0xff);

    // Output size: 1280x720
    await w8(dev, 0x3808, (1280 >> 8) & 0x0f);
    await w8(dev, 0x3809, 1280 & 0xff);
    await w8(dev, 0x380a, (720 >> 8) & 0x7f);
    await w8(dev, 0x380b, 720 & 0xff);

    // Timing: HTS=1896, VTS=984
    await w8(dev, 0x380c, (1896 >> 8) & 0x1f);
    await w8(dev, 0x380d, 1896 & 0xff);
    await w8(dev, 0x380e, (984 >> 8) & 0xff);
    await w8(dev, 0x380f, 984 & 0xff);

    // 3:1 binning both directions, vertical mirror
    await w8(dev, 0x3814, 0x31);
    await w8(dev, 0x3815, 0x31);
    await w8(dev, 0x3821, 0x01);

    // MIPI pclk period, analog path, output format
    await w8(dev, 0x4837, 36);
    await w8(dev, 0x3618, 0x00);
    await w8(dev, 0x3612, 0x59);
    await w8(dev, 0x3708, 0x64);
    await w8(dev, 0x3709, 0x52);
    await w8(dev, 0x370c, 0x03);
    await w8(dev, 0x4300, 0x00);
    await w8(dev, 0x501f, 0x03); // RGB output via ISP

    await w8(dev, 0x3008, 0x02); // Power on
}

/**
 * Dispatch resolution configuration to the appropriate setup function.
 */
async function ov5640SetupResolution(w8: I2CWrite8, dev: number, width: number, height: number): Promise<void> {
    if (width === 1920 && height === 1080) {
        await ov5640Setup1080p(w8, dev);
    } else if (width === 1280 && height === 720) {
        await ov5640Setup720p(w8, dev);
    } else {
        throw new Error(`Unsupported OV5640 resolution: ${width}x${height}`);
    }
}

// ============================================================
// PCAMCameraControl
// ============================================================

/**
 * Camera control implementation for OV5640 (Digilent PCAM) on SZG-MIPI-8320.
 * Uses the pcam bitfile with TPG in passthrough mode.
 *
 * Translated from Python reference: Gateware/.../Software/pcam.py
 */
export class PCAMCameraControl implements ICameraControl {
    private readonly _i2c: I2CController;
    private readonly _w8: I2CWrite8;
    private readonly _dev7bit = OV5640_I2C_ADDR_7BIT;

    private _size: IMatrixDimensions;
    private _skips: IMatrixDimensions;
    private _frameDimensions: IMatrixDimensions;
    private _exposure: CameraExposure = 0;

    constructor(axiLite: IAxiLite) {
        this._i2c = new I2CController(axiLite);
        this._size = OV5640_DEFAULT_SIZE;
        this._skips = { columnCount: 0, rowCount: 0 };
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);

        this._w8 = this._i2c.write8.bind(this._i2c);
    }

    // ================================================================
    // ICameraControl Accessors
    // ================================================================

    public get defaultSize(): MatrixDimensions {
        return OV5640_DEFAULT_SIZE;
    }

    public get supportedSkips(): MatrixDimensions[] {
        return [{ rowCount: 0, columnCount: 0 }];
    }

    public get supportedTestModes(): TestMode[] {
        return TPG_SUPPORTED_TEST_MODES;
    }

    public get supportedFrameConfigurations(): FrameConfiguration[] {
        return OV5640_RESOLUTIONS.map((dimensions) => ({
            dimensions,
            skips: { rowCount: 0, columnCount: 0 }
        }));
    }

    public get frameDimensions(): IMatrixDimensions {
        return this._frameDimensions;
    }

    public get exposure(): CameraExposure {
        return this._exposure;
    }

    // ================================================================
    // ICameraControl Operations
    // ================================================================

    public async initialize(): Promise<void> {
        await this._i2c.initialize();

        // Initialize OV5640 sensor registers (sensor remains powered down after)
        await ov5640Init(this._w8, this._dev7bit);
        await ov5640AwbInit(this._w8, this._dev7bit);

        // Configure for 1080p and power up sensor
        await ov5640Setup1080p(this._w8, this._dev7bit);

        // Reset tracked size to match the 1080p we just configured.
        // Without this, setSize() during setCameraState() sees a stale
        // _size (e.g. 1280x720 from previous session), thinks the resolution
        // hasn't changed, and skips ov5640SetupResolution().
        this._size = OV5640_DEFAULT_SIZE;
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);

        console.log("PCAMCameraControl::initialize() Complete");
    }

    /**
     * Set the AEC (Auto Exposure Control) luminance target on the OV5640.
     *
     * The value (0-247) sets the target average luminance for the sensor's
     * internal auto-exposure engine. The AEC adjusts actual integration time
     * and gain autonomously to reach this brightness target. Higher values
     * produce a brighter image; lower values produce a darker image.
     *
     * The four AEC registers define a hysteresis band (BPT/WPT) that the
     * AEC engine uses to determine when to start/stop adjusting exposure.
     *
     * The sensor is power-cycled around the register writes to ensure the
     * AEC state machine latches the new values. This power-cycle also
     * re-establishes the MIPI link when called after a pipeline reset.
     *
     * @param exposure  AEC luminance target (0-247, unitless brightness scale).
     */
    public async setExposure(exposure: CameraExposure): Promise<void> {
        this._exposure = exposure;
        const exposureValue = Math.max(0, Math.min(247, exposure));
        const w8 = this._w8;
        const dev = this._dev7bit;

        await w8(dev, OV5640_REG_SYSTEM_CTRL, 0x42); // Power down
        await w8(dev, OV5640_REG_AEC_CTRL0F, exposureValue + 8); // Max enter
        await w8(dev, OV5640_REG_AEC_CTRL10, exposureValue);     // Min enter
        await w8(dev, OV5640_REG_AEC_CTRL1B, exposureValue + 8); // Max go out
        await w8(dev, OV5640_REG_AEC_CTRL1E, exposureValue);     // Min go out
        await w8(dev, OV5640_REG_SYSTEM_CTRL, 0x02); // Power on
    }

    public async setSize(size: MatrixDimensions): Promise<void> {
        const resolutionChanged =
            size.columnCount !== this._size.columnCount ||
            size.rowCount !== this._size.rowCount;

        if (resolutionChanged) {
            await ov5640SetupResolution(this._w8, this._dev7bit, size.columnCount, size.rowCount);
        }

        this._size = size;
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async setSkips(_skips: MatrixDimensions): Promise<void> {
        // OV5640 does not support skip-based subsampling.
        this._skips = { rowCount: 0, columnCount: 0 };
        this._frameDimensions = calculateFrameDimensions(this._size, this._skips);
    }

    public async reinitializeI2C(): Promise<void> {
        await this._i2c.initialize();
    }
}
