/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// GuiCameraBackend.cpp — see header. Shared hardware bring-up + capture flow.

#include "GuiCameraBackend.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "ISPDriver.h"
#include "NullISPDriver.h"
#include "PCAMCameraControl.h"
#include "SYZYGYCameraControl.h"
#include "TPGCameraControl.h"
#include "TPGPatterns.h"

namespace okcli {
namespace {

int ec(OpalKelly::ErrorCode e) { return static_cast<int>(e); }  // NoError == 0

// Read the SYZYGY Port-A camera model for an open device ("" if none/unsupported).
std::string readCameraModel(OpalKelly::FrontPanel& fp) {
    OpalKelly::DeviceSettings settings;
    if (ec(fp.GetDeviceSettings(settings)) != 0) return "";
    std::string raw, model;
    if (ec(settings.GetString(kSyzygyProductModelKey, &raw)) != 0) return "";
    productModelToCameraModel(raw, model);
    return model;
}

DeviceConfiguration configForModeBoard(bool isGen3, CameraMode mode) {
    switch (mode) {
        case CameraMode::Tpg:
            return isGen3 ? DeviceConfiguration::HUB1450_TPG : DeviceConfiguration::XEM8320_TPG;
        case CameraMode::Pcam:
            return isGen3 ? DeviceConfiguration::HUB1450_PCAM : DeviceConfiguration::XEM8320_PCAM;
        case CameraMode::SzgCam:
            return isGen3 ? DeviceConfiguration::HUB1450_SZG_Camera
                          : DeviceConfiguration::XEM8320_SZG_Camera;
    }
    return DeviceConfiguration::XEM8320_TPG;
}

}  // namespace

std::vector<GuiCameraBackend::DeviceEntry> GuiCameraBackend::discover(const std::string& realm) {
    std::vector<DeviceEntry> out;
    try {
        OpalKelly::FrontPanelDevices devices(realm);
        const int count = devices.GetCount();
        for (int i = 0; i < count; ++i) {
            const std::string serial = devices.GetSerial(i);
            OpalKelly::FrontPanelPtr fp = devices.Open(serial);
            if (!fp) continue;
            okTDeviceInfo di;
            std::memset(&di, 0, sizeof(di));
            if (ec(fp->GetDeviceInfo(&di)) != 0) continue;

            DeviceEntry e;
            e.serial = serial;
            e.productName = di.productName;
            e.isClassic = (di.deviceInterface != OK_INTERFACE_GEN3);
            e.cameraModel = readCameraModel(*fp);
            e.config = determineConfiguration(e.cameraModel, !e.isClassic);
            e.mode = cameraModeFor(e.config);
            out.push_back(std::move(e));
        }
    } catch (...) {
        // Enumeration failures yield an empty/partial list rather than throwing to the GUI.
    }
    return out;
}

bool GuiCameraBackend::describe(OpalKelly::FrontPanel& fp, const std::string& serial,
                                DeviceEntry& out) {
    try {
        okTDeviceInfo di;
        std::memset(&di, 0, sizeof(di));
        if (ec(fp.GetDeviceInfo(&di)) != 0) return false;
        out.serial = serial;
        out.productName = di.productName;
        out.isClassic = (di.deviceInterface != OK_INTERFACE_GEN3);
        out.cameraModel = readCameraModel(fp);
        out.config = determineConfiguration(out.cameraModel, !out.isClassic);
        out.mode = cameraModeFor(out.config);
        return true;
    } catch (...) {
        return false;
    }
}

bool GuiCameraBackend::open(const std::string& serial, const std::string& bitfilesRoot,
                            const std::string& realm, const std::string& modeOverride,
                            std::string& err) {
    close();
    try {
        OpalKelly::FrontPanelDevices devices(realm);
        m_fp = devices.Open(serial);
        if (!m_fp) { err = "could not open device " + serial; return false; }
        return configureAndBuild(bitfilesRoot, modeOverride, err);
    } catch (const AxiError& e) {
        err = e.what();
        close();
        return false;
    }
}

bool GuiCameraBackend::open(OpalKelly::FrontPanelPtr fp, const std::string& bitfilesRoot,
                            const std::string& modeOverride, std::string& err) {
    close();
    if (!fp) { err = "null device handle"; return false; }
    m_fp = std::move(fp);
    return configureAndBuild(bitfilesRoot, modeOverride, err);
}

bool GuiCameraBackend::configureAndBuild(const std::string& bitfilesRoot,
                                         const std::string& modeOverride, std::string& err) {
    try {
        // Identify board + camera from the open handle, then resolve config/bitfile.
        okTDeviceInfo di;
        std::memset(&di, 0, sizeof(di));
        if (ec(m_fp->GetDeviceInfo(&di)) != 0) { err = m_fp->GetLastErrorMessage(); close(); return false; }
        const bool isGen3 = (di.deviceInterface == OK_INTERFACE_GEN3);
        const std::string serial = di.serialNumber;
        const std::string productName = di.productName;
        const std::string cameraModel = readCameraModel(*m_fp);

        DeviceConfiguration config = determineConfiguration(cameraModel, isGen3);
        CameraMode mode = cameraModeFor(config);
        if (!modeOverride.empty()) {
            if (modeOverride == "tpg") mode = CameraMode::Tpg;
            else if (modeOverride == "pcam") mode = CameraMode::Pcam;
            else if (modeOverride == "szgcam") mode = CameraMode::SzgCam;
            else { err = "invalid mode '" + modeOverride + "'"; close(); return false; }
            config = configForModeBoard(isGen3, mode);
        }

        const std::string bitfile = bitfilesRoot + "/" + bitfileFor(config);
        if (ec(m_fp->ConfigureFPGA(bitfile)) != 0) {
            err = std::string("ConfigureFPGA failed: ") + m_fp->GetLastErrorMessage();
            close();
            return false;
        }
        // Give the freshly-configured FPGA and camera a moment to settle before the sensor bring-up.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        m_transport = makeTransport(*m_fp, isGen3);

        // Build the IP drivers + camera control + sequencer.
        IAxiLite& axi = *m_transport.axiLite;
        if (mode == CameraMode::Tpg) m_isp.reset(new NullISPDriver());
        else m_isp.reset(new ISPDriver(axi));
        m_tpg.reset(new TPGDriver(axi));
        switch (mode) {
            case CameraMode::Tpg:    m_control.reset(new TPGCameraControl()); break;
            case CameraMode::Pcam:   m_control.reset(new PCAMCameraControl(axi)); break;
            case CameraMode::SzgCam: m_control.reset(new SYZYGYCameraControl(axi)); break;
        }
        m_seq.reset(new CapturePipelineSequencer(axi, *m_transport.axiStream, mode, *m_isp, *m_tpg,
                                                 *m_control));

        m_info.serial = serial;
        m_info.productName = productName;
        m_info.isClassic = !isGen3;
        m_info.cameraModel = cameraModel;
        m_info.config = config;
        m_info.mode = mode;
        maybeSetupScripted();
        m_started = false;
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        close();
        return false;
    }
}

bool GuiCameraBackend::startPipeline(double exposure, int rgain, int ggain, int bgain, int awb,
                                     int testPattern, std::string& err) {
    if (!m_seq) { err = "no device open"; return false; }
    if (m_scripted) return scriptedStart(exposure, rgain, ggain, bgain, awb, err);
    try {
        m_seq->assertPipelineResets();
        m_control->initialize();
        m_seq->initializePipeline();

        m_control->setExposure(exposure);
        m_isp->setGains(static_cast<uint32_t>(rgain), static_cast<uint32_t>(ggain),
                        static_cast<uint32_t>(bgain));
        m_isp->setAWBThreshold(static_cast<uint32_t>(awb));

        const FrameConfiguration fc = m_control->supportedFrameConfigurations().front();
        m_control->setSize(fc.dimensions);
        m_control->setSkips(fc.skips);

        const uint32_t pattern =
            testPattern >= 0 ? static_cast<uint32_t>(testPattern)
                             : (m_info.mode == CameraMode::Tpg ? TPG_PATTERN_HORIZONTAL_RAMP
                                                               : TPG_PATTERN_PASSTHROUGH);
        m_tpg->setPattern(pattern);

        const MatrixDimensions dims = m_control->frameDimensions();
        m_seq->setResolution(dims.columnCount, dims.rowCount);
        m_seq->setFrameDimensions(dims);
        m_seq->logicReset();
        m_started = true;
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::warmup(int ms, std::string& err) {
    if (!m_seq || !m_started) { err = "pipeline not started"; return false; }
    if (m_scripted) return true;  // scripted capture warms up server-side per-call
    if (m_info.mode == CameraMode::Tpg || ms <= 0) return true;  // TPG is instant
    try {
        CapturedFrame w;
        auto bright = [](const CapturedFrame& f) {
            uint8_t mx = 0;
            for (std::size_t i = 0; i < f.image.size(); i += 997)
                if (f.image[i] > mx) mx = f.image[i];
            return mx > 32;
        };
        const auto t0 = std::chrono::steady_clock::now();
        const long capMs = ms * 3L;
        while (m_seq->captureFrame(w)) {
            const long el = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();
            if (el >= ms && bright(w)) break;
            if (el >= capMs) break;
        }
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::captureOnce(Frame& out, std::string& err) {
    if (!m_seq || !m_started) { err = "pipeline not started"; return false; }
    if (m_scripted) return scriptedCapture(out, 8, err);  // discard a few server-side, return fresh
    try {
        CapturedFrame f;
        if (!m_seq->captureOnce(f)) { err = "capture returned no frame"; return false; }
        gbrToRgb(f, out);
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::captureFrame(Frame& out, std::string& err) {
    if (!m_seq || !m_started) { err = "pipeline not started"; return false; }
    if (m_scripted) return scriptedCapture(out, 0, err);  // continuous: one fresh frame per call
    try {
        CapturedFrame f;
        if (!m_seq->captureFrame(f)) { err = "capture returned no frame"; return false; }
        gbrToRgb(f, out);
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::setGains(int rgain, int ggain, int bgain, std::string& err) {
    if (m_scripted) {
        try {
            OpalKelly::ScriptValues a;
            for (int v : {rgain, ggain, bgain}) a.Add(OpalKelly::ScriptValue(v));
            m_engine->RunScriptFunction("SetGainsLive", a);
            return true;
        } catch (const std::exception& e) { err = e.what(); return false; }
    }
    if (!m_isp) { err = "no device open"; return false; }
    try {
        m_isp->setGains(static_cast<uint32_t>(rgain), static_cast<uint32_t>(ggain),
                        static_cast<uint32_t>(bgain));
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::setExposure(double exposure, std::string& err) {
    if (m_scripted) {
        try {
            OpalKelly::ScriptValues a;
            a.Add(OpalKelly::ScriptValue(cameraKindString(m_info.mode)));
            a.Add(OpalKelly::ScriptValue(static_cast<int>(exposure)));
            m_engine->RunScriptFunction("SetExposureLive", a);
            return true;
        } catch (const std::exception& e) { err = e.what(); return false; }
    }
    if (!m_control) { err = "no device open"; return false; }
    try {
        m_control->setExposure(exposure);
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::setAWB(int threshold, std::string& err) {
    if (m_scripted) {
        try {
            OpalKelly::ScriptValues a;
            a.Add(OpalKelly::ScriptValue(threshold));
            m_engine->RunScriptFunction("SetAwbLive", a);
            return true;
        } catch (const std::exception& e) { err = e.what(); return false; }
    }
    if (!m_isp) { err = "no device open"; return false; }
    try {
        m_isp->setAWBThreshold(static_cast<uint32_t>(threshold));
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::setMotionSpeed(int speed, std::string& err) {
    if (m_scripted) {
        try {
            OpalKelly::ScriptValues a;
            a.Add(OpalKelly::ScriptValue(speed));
            m_engine->RunScriptFunction("SetMotionSpeedLive", a);
            return true;
        } catch (const std::exception& e) { err = e.what(); return false; }
    }
    if (!m_tpg) { err = "no device open"; return false; }
    try {
        m_tpg->setMotionSpeed(static_cast<uint32_t>(speed));
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::setCameraMode(int testMode, std::string& err) {
    // Image Capture (testMode < 0) → passthrough for real sensors, horizontal ramp for TPG-only.
    const uint32_t fallback =
        (m_info.mode == CameraMode::Tpg) ? TPG_PATTERN_HORIZONTAL_RAMP : TPG_PATTERN_PASSTHROUGH;
    const uint32_t pattern = (testMode < 0)
                                 ? fallback
                                 : testModeToPatternId(static_cast<TestMode>(testMode), fallback);
    if (m_scripted) {
        try {
            OpalKelly::ScriptValues a;
            a.Add(OpalKelly::ScriptValue(static_cast<int>(pattern)));
            m_engine->RunScriptFunction("SetPatternLive", a);
            return true;
        } catch (const std::exception& e) { err = e.what(); return false; }
    }
    if (!m_tpg) { err = "no device open"; return false; }
    try {
        m_tpg->setPattern(pattern);
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::setResolution(int frameConfigIndex, std::string& err) {
    if (m_scripted) {
        try {
            const auto configs = m_control->supportedFrameConfigurations();
            if (frameConfigIndex < 0 || frameConfigIndex >= static_cast<int>(configs.size())) {
                err = "resolution index out of range";
                return false;
            }
            // fc.dimensions is the output frame size (w=col, h=row) for both cameras; skips are the
            // sensor subsampling factors (szgcam only). Retarget server-side, then track the new size.
            const FrameConfiguration& fc = configs[frameConfigIndex];
            OpalKelly::ScriptValues a;
            a.Add(OpalKelly::ScriptValue(cameraKindString(m_info.mode)));
            a.Add(OpalKelly::ScriptValue(static_cast<int>(fc.dimensions.columnCount)));
            a.Add(OpalKelly::ScriptValue(static_cast<int>(fc.dimensions.rowCount)));
            a.Add(OpalKelly::ScriptValue(static_cast<int>(fc.skips.columnCount)));
            a.Add(OpalKelly::ScriptValue(static_cast<int>(fc.skips.rowCount)));
            m_engine->RunScriptFunction("SetResolutionLive", a);
            m_scriptW = static_cast<int>(fc.dimensions.columnCount);
            m_scriptH = static_cast<int>(fc.dimensions.rowCount);
            return true;
        } catch (const std::exception& e) { err = e.what(); return false; }
    }
    if (!m_seq || !m_control || !m_started) { err = "pipeline not started"; return false; }
    try {
        const auto configs = m_control->supportedFrameConfigurations();
        if (frameConfigIndex < 0 || frameConfigIndex >= static_cast<int>(configs.size())) {
            err = "resolution index out of range";
            return false;
        }
        const FrameConfiguration& fc = configs[frameConfigIndex];
        m_control->setSize(fc.dimensions);
        m_control->setSkips(fc.skips);
        const MatrixDimensions dims = m_control->frameDimensions();
        m_seq->setResolution(dims.columnCount, dims.rowCount);
        m_seq->setFrameDimensions(dims);
        m_seq->logicReset();
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

bool GuiCameraBackend::restart(double exposure, int rgain, int ggain, int bgain, int awb,
                               int testMode, std::string& err) {
    if (!m_seq) { err = "no device open"; return false; }
    const uint32_t fallback =
        (m_info.mode == CameraMode::Tpg) ? TPG_PATTERN_HORIZONTAL_RAMP : TPG_PATTERN_PASSTHROUGH;
    const int pattern = (testMode < 0)
                            ? -1
                            : static_cast<int>(testModeToPatternId(static_cast<TestMode>(testMode),
                                                                   fallback));
    return startPipeline(exposure, rgain, ggain, bgain, awb, pattern, err);
}

std::vector<TestMode> GuiCameraBackend::supportedTestModes() const {
    return m_control ? m_control->supportedTestModes() : std::vector<TestMode>{};
}

std::vector<FrameConfiguration> GuiCameraBackend::supportedFrameConfigurations() const {
    return m_control ? m_control->supportedFrameConfigurations() : std::vector<FrameConfiguration>{};
}

bool GuiCameraBackend::resetPipeline(std::string& err) {
    if (m_scripted) return true;  // scripted pipeline stays up (use restart to re-bring-up over Lua)
    if (!m_seq || !m_started) { err = "pipeline not started"; return false; }
    try {
        m_seq->logicReset();
        return true;
    } catch (const AxiError& e) {
        err = e.what();
        return false;
    }
}

MatrixDimensions GuiCameraBackend::frameDimensions() const {
    return m_seq ? m_seq->frameDimensions() : MatrixDimensions{0, 0};
}

void GuiCameraBackend::close() {
    // Destroy in reverse dependency order: sequencer/control/drivers borrow the transport, which
    // borrows the device's data ports.
    m_seq.reset();
    m_control.reset();
    m_tpg.reset();
    m_isp.reset();
    m_engine.reset();          // scripted engine borrows m_fp — release before it
    m_scripted = false;
    m_scriptW = m_scriptH = 0;
    m_transport = Transport{};
    m_fp.reset();
    m_started = false;
    m_info = DeviceEntry{};
}

const char* GuiCameraBackend::cameraKindString(CameraMode mode) {
    switch (mode) {
        case CameraMode::SzgCam: return "szgcam";
        case CameraMode::Pcam:   return "pcam";
        default:                 return "";  // TPG / unknown → not scripted
    }
}

// Set up the server-side scripted path if eligible: a Lua path is configured, the device is remote
// (FPoIP), and it's a real camera. Any failure falls back silently to the C++-direct path.
void GuiCameraBackend::maybeSetupScripted() {
    m_scripted = false;
    m_engine.reset();
    if (m_luaPath.empty()) {
        if (const char* env = std::getenv("OKCAM_LUA")) m_luaPath = env;
    }
    if (m_luaPath.empty() || !m_fp || !m_fp->IsRemote()) return;  // scripted only over FPoIP
    if (!cameraKindString(m_info.mode)[0]) return;                // TPG → C++-direct
    try {
        m_engine.reset(new OpalKelly::ScriptEngine());
        m_engine->ConstructLua(*m_fp);     // okFP in Lua := this remote device (runs server-side)
        m_engine->LoadFile(m_luaPath);
        m_engine->RunScriptFunction("Setup");
        m_scripted = true;
    } catch (const std::exception&) {
        m_engine.reset();
        m_scripted = false;  // fall back to the C++-direct path
    }
}

// Server-side camera bring-up + pipeline via the Lua (one scripted call instead of hundreds of
// FPoIP round-trips). Applies the same settings as startPipeline.
bool GuiCameraBackend::scriptedStart(double exposure, int rgain, int ggain, int bgain, int awb,
                                     std::string& err) {
    try {
        const MatrixDimensions sz = m_control->defaultSize();  // {row, col}
        m_scriptW = sz.columnCount;
        m_scriptH = sz.rowCount;
        OpalKelly::ScriptValues sa;
        sa.Add(OpalKelly::ScriptValue(cameraKindString(m_info.mode)));
        for (int v : {m_scriptW, m_scriptH, static_cast<int>(exposure), rgain, ggain, bgain, awb})
            sa.Add(OpalKelly::ScriptValue(v));
        m_engine->RunScriptFunction("CameraStart", sa);
        m_started = true;
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// Convert one scripted (img, hist) buffer pair into an RGB Frame at the tracked scripted size.
bool GuiCameraBackend::unpackScriptedFrame(const OpalKelly::Buffer& img, const OpalKelly::Buffer* hist,
                                           Frame& out, std::string& err) {
    const std::size_t px = static_cast<std::size_t>(m_scriptW) * m_scriptH;
    if (img.GetSize() < px * 3) { err = "scripted capture: short frame"; return false; }
    const unsigned char* gbr = img.GetData();
    out.width = m_scriptW;
    out.height = m_scriptH;
    out.changed = true;
    // Histogram: the Lua returns it as a second buffer (u32 bins, GBR order).
    out.histogram.clear();
    if (hist && hist->GetSize() >= 4) {
        const unsigned char* hd = hist->GetData();
        const std::size_t n = hist->GetSize() / 4;
        out.histogram.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            out.histogram[i] = static_cast<uint32_t>(hd[i * 4]) |
                               (static_cast<uint32_t>(hd[i * 4 + 1]) << 8) |
                               (static_cast<uint32_t>(hd[i * 4 + 2]) << 16) |
                               (static_cast<uint32_t>(hd[i * 4 + 3]) << 24);
    }
    out.rgb.resize(px * 3);
    for (std::size_t i = 0; i < px; ++i) {        // GBR wire → RGB
        out.rgb[i * 3 + 0] = gbr[i * 3 + 2];
        out.rgb[i * 3 + 1] = gbr[i * 3 + 0];
        out.rgb[i * 3 + 2] = gbr[i * 3 + 1];
    }
    return true;
}

// Capture one frame via the Lua (warmupFrames discarded server-side first), returned as RGB.
bool GuiCameraBackend::scriptedCapture(Frame& out, int warmupFrames, std::string& err) {
    try {
        OpalKelly::ScriptValues ca;
        ca.Add(OpalKelly::ScriptValue(warmupFrames));
        OpalKelly::ScriptValues rv = m_engine->RunScriptFunction("CaptureFrame", ca);
        OpalKelly::Buffer img, hbuf;
        if (rv.GetCount() < 1 || !rv.Get(0).GetAsBuffer(&img)) {
            err = "scripted capture: no frame"; return false;
        }
        const bool haveHist = rv.GetCount() >= 2 && rv.Get(1).GetAsBuffer(&hbuf);
        return unpackScriptedFrame(img, haveHist ? &hbuf : nullptr, out, err);
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

void GuiCameraBackend::gbrToRgb(const CapturedFrame& in, Frame& out) {
    out.width = in.width;
    out.height = in.height;
    out.changed = in.frameChanged;
    out.histogram = in.histogram;
    const std::size_t n = static_cast<std::size_t>(in.width) * in.height;
    out.rgb.resize(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        out.rgb[i * 3 + 0] = in.image[i * 3 + 2];  // R
        out.rgb[i * 3 + 1] = in.image[i * 3 + 0];  // G
        out.rgb[i * 3 + 2] = in.image[i * 3 + 1];  // B
    }
}

}  // namespace okcli
