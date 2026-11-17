/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// GuiCameraBackend.h — GUI-facing facade over the camera backend.
//
// Assembles the transport + IISP/ITPG + camera control + CapturePipelineSequencer behind a small, synchronous, GUI-friendly API: enumerate, open,
// bring the pipeline up with settings, capture frames (returned as RGB), reset, close.
//
// The GUI's worker thread owns one of these and calls it for the hardware bring-up + capture flow.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "okFrontPanel.h"

#include "AudioPipe.h"
#include "AudioStream.h"
#include "CameraConfig.h"
#include "CameraTypes.h"
#include "CapturePipelineSequencer.h"
#include "ICameraControl.h"
#include "IISP.h"
#include "TPGDriver.h"
#include "Transport.h"
#include "UserIoDriver.h"

namespace okcli {

class GuiCameraBackend {
public:
    // One enumerated device's identity (for a device-selection list).
    struct DeviceEntry {
        std::string serial;
        std::string productName;
        bool isClassic = false;            // true = XEM8320/XEM8310 (classic); false = Hub (AXI/GEN3)
        std::string cameraModel;           // SYZYGY product model ("" if none)
        DeviceConfiguration config = DeviceConfiguration::XEM8320_TPG;
        CameraMode mode = CameraMode::Tpg;
    };

    // A captured frame converted to packed RGB (8-bit, width*height*3), plus the histogram.
    struct Frame {
        std::vector<uint8_t> rgb;
        int width = 0;
        int height = 0;
        std::vector<uint32_t> histogram;
        bool changed = false;
    };

    GuiCameraBackend() = default;
    ~GuiCameraBackend() { close(); }

    GuiCameraBackend(const GuiCameraBackend&) = delete;
    GuiCameraBackend& operator=(const GuiCameraBackend&) = delete;

    // Enumerate devices on a realm ("" = local USB; "fpoip://..." = remote). Each is opened
    // briefly to read its identity + attached camera. Never throws.
    static std::vector<DeviceEntry> discover(const std::string& realm = std::string());

    // Read a device's identity + attached camera from an already-open handle. Used by the
    // FrontPanel hot-plug monitor (which owns the open) to describe a newly-added device.
    static bool describe(OpalKelly::FrontPanel& fp, const std::string& serial, DeviceEntry& out);

    // Open a device by serial on a realm, ConfigureFPGA the matching bitfile (resolved from
    // bitfilesRoot), and build the driver stack. Returns false with err set on failure.
    // modeOverride ("","tpg","pcam","szgcam","imx219") forces the mode/bitfile.
    bool open(const std::string& serial, const std::string& bitfilesRoot, const std::string& realm,
              const std::string& modeOverride, std::string& err);

    // Adopt an already-open device handle (opened by a FrontPanelManager, so all opens go through
    // one authority), ConfigureFPGA + build the stack. Used by the GUI.
    bool open(OpalKelly::FrontPanelPtr fp, const std::string& bitfilesRoot,
              const std::string& modeOverride, std::string& err);

    // Bring up the sensor + pipeline with the given settings (assertPipelineResets → control.initialize → setExposure/gains/awb/size/skips → tpg pattern →
    // sequencer.logicReset). testPattern < 0 = image capture (passthrough / TPG ramp); >= 0 = a TPG
    // pattern id. Must be called after open().
    bool startPipeline(double exposure, int rgain, int ggain, int bgain, int awb, int testPattern,
                       std::string& err);

    // Discard frames for ~ms wall-clock (and until a non-black frame), letting a real sensor's AEC
    // settle. No-op for TPG. Safe to skip in continuous mode (the GUI streams).
    bool warmup(int ms, std::string& err);

    // Capture a single frame (discards one stale triple-buffer frame, returns the fresh one).
    bool captureOnce(Frame& out, std::string& err);
    // Capture one frame (continuous-loop body).
    bool captureFrame(Frame& out, std::string& err);

    // Update settings live (no pipeline reset) — safe per ISP/sensor semantics.
    bool setGains(int rgain, int ggain, int bgain, std::string& err);
    bool setExposure(double exposure, std::string& err);
    bool setAWB(int threshold, std::string& err);            // ISP auto-white-balance threshold 0..255
    bool setMotionSpeed(int speed, std::string& err);        // TPG motion speed 0..255
    // Set the capture mode: testMode < 0 = image capture (passthrough / TPG ramp); else a TestMode.
    bool setCameraMode(int testMode, std::string& err);

    // Re-assert the capture-pipeline logic reset (the "Pipeline Reset" action).
    bool resetPipeline(std::string& err);

    // Switch to a supported resolution by index (into supportedFrameConfigurations): setSize+setSkips
    // +setResolution+setFrameDimensions+logicReset.
    bool setResolution(int frameConfigIndex, std::string& err);

    // Full device restart with the given settings: re-run the pipeline bring-up = assertResets +
    // sensor initialize + pipeline init + apply + logicReset.
    // testMode < 0 = image capture; else a TestMode value.
    bool restart(double exposure, int rgain, int ggain, int bgain, int awb, int testMode,
                 std::string& err);

    // Selector data for the GUI (valid after open()).
    std::vector<TestMode> supportedTestModes() const;
    std::vector<FrameConfiguration> supportedFrameConfigurations() const;
    bool isTpgOnly() const { return m_info.mode == CameraMode::Tpg; }

    // Path to camera_axi.lua for the server-side scripted path. When set AND the device is remote
    // (FPoIP) AND it's a real camera (szgcam/pcam), bring-up + capture run server-side via the Lua
    // ScriptEngine (collapses the FPoIP per-transaction round-trips). Local/TPG always use C++-direct.
    // Empty/missing/unset → C++-direct everywhere (safe fallback). Call before open().
    void setLuaPath(const std::string& path) { m_luaPath = path; }
    bool isScripted() const { return m_scripted; }

    // ---- Microphones (64 PDM) and board user I/O (XEM8310 / DARWIN) -----------------------------------------------
    // All calls below except audio() / audioRunning() / audioError() must be made from the thread that owns the device
    // (the GUI's camera worker), like every other call of this class.
    //
    // Audio is serviced from that same thread: (a) between slices of every large video transfer (the gateware only
    // buffers a few ms of audio in front of its DDR4 ring) and (b) by audioPump(), which the worker loop calls when no
    // video transfer is running.
    bool audioSupported() const;                       // XEM8310 (classic device, IMX219 configuration)
    bool audioPresent(std::string& err);               // the loaded bitfile contains the microphone subsystem
    bool audioStart(std::string& err);                 // resets + enables the microphones, waits for them to run
    void audioStop();                                  // stops a recording, disables the microphones
    bool audioRunning() const { return m_audioRunning; }
    // One service step: read what is waiting, else wait for one packet. False (+ err) on a failure: audio stops.
    bool audioPump(std::string& err);
    bool audioCounters(MicCounters& out, std::string& err);
    // Thread-safe object for the GUI: statistics, waveform peaks, recording start/stop (nullptr until audioStart()).
    // The GUI keeps its own reference: statistics, peaks and recording never touch the device, so they stay valid (and
    // harmless) after the device is closed.
    std::shared_ptr<AudioCapture> audio() { return m_audio; }
    std::string audioError() const;                    // last error raised while servicing audio between video slices
    UserIoDriver* userIo();                            // LEDs + buttons; nullptr if the device has none

    void close();

    bool isOpen() const { return static_cast<bool>(m_fp); }
    bool isRemote() const { return m_fp && m_fp->IsRemote(); }
    const DeviceEntry& info() const { return m_info; }
    MatrixDimensions frameDimensions() const;

private:
    // With m_fp already set (open), read identity, ConfigureFPGA the matching bitfile, build the
    // driver stack, and populate m_info. Shared by both open() overloads.
    bool configureAndBuild(const std::string& bitfilesRoot, const std::string& modeOverride,
                           std::string& err);

    // GBR wire order (byte0=G, byte1=B, byte2=R) → packed RGB.
    static void gbrToRgb(const CapturedFrame& in, Frame& out);

    // Server-side scripted path (FPoIP). Set up the ScriptEngine if eligible; capture via Lua.
    void maybeSetupScripted();
    bool scriptedStart(double exposure, int rgain, int ggain, int bgain, int awb, std::string& err);
    bool scriptedCapture(Frame& out, int warmupFrames, std::string& err);
    // Convert one scripted (img, hist) buffer pair (GBR wire order) into an RGB Frame at m_scriptW/H.
    bool unpackScriptedFrame(const OpalKelly::Buffer& img, const OpalKelly::Buffer* hist, Frame& out,
                             std::string& err);
    static const char* cameraKindString(CameraMode mode);

    OpalKelly::FrontPanelPtr m_fp;          // owns the device; transport borrows its data ports
    Transport m_transport;
    std::unique_ptr<IISP> m_isp;
    std::unique_ptr<TPGDriver> m_tpg;
    std::unique_ptr<ICameraControl> m_control;
    std::unique_ptr<CapturePipelineSequencer> m_seq;
    DeviceEntry m_info;
    bool m_started = false;

    // Microphones / user I/O (created on first use)
    std::unique_ptr<AudioPipeOverClassicDataPort> m_audioPipe;
    std::shared_ptr<AudioCapture> m_audio;             // shared with the GUI; declared after the pipe it references
    std::atomic<bool> m_audioRunning{false};
    mutable std::mutex m_audioErrMx;
    std::string m_audioErr;
    std::unique_ptr<UserIoDriver> m_userIo;

    std::string m_luaPath;                  // camera_axi.lua (server-side scripted path); "" = off
    std::unique_ptr<OpalKelly::ScriptEngine> m_engine;  // reset on close (borrows m_fp)
    bool m_scripted = false;
    int m_scriptW = 0, m_scriptH = 0;
};

}  // namespace okcli
