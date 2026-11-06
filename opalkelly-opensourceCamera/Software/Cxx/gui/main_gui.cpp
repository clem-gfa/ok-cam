/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// main_gui.cpp — wxWidgets camera GUI driving the camera backend (GuiCameraBackend).
//
// THREADING/QUEUING uses a joinable wxThread (closures
// in a wxMessageQueue; empty action = quit; block Receive idle / ReceiveTimeout(0) streaming +
// capture on timeout; try/catch+MISC_ERROR; ONE wxEVT_THREAD event + Result-enum discriminant +
// device-id payload + late-event filtering; single lock-free image buffer; memcpy+Refresh in the
// handler + render in OnPaint; ClearCameraThreadQueue+Quit+Unbind+Wait shutdown; IsMain/
// CheckCalledInCameraThread asserts).
//
// DEVICE ENUMERATION uses a FrontPanelManager monitor
// (separate thread) calls OnDeviceAdded/OnDeviceRemoved (marshaled to the GUI thread via CallAfter,
// with the same pending-until-StartDeviceProcessing handshake); the GUI probes each added device
// for its identity. (Hot-plug add/remove updates the device list live.)

#include <wx/wx.h>
#include <wx/bmpbndl.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/msgqueue.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stdpaths.h>
#include <wx/tglbtn.h>
#include <wx/xrc/xmlres.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "GuiCameraBackend.h"
#include "RgbViewport.h"
#include "okFrontPanel.h"

using okcli::GuiCameraBackend;

// Device capabilities passed to the GUI on SetupGood, so it can build the mode/size selectors
// (supported test modes + frame configurations, read from the camera control after open).
struct DeviceCaps {
    wxString name;
    bool tpgOnly = false;
    std::vector<int> testModes;                       // TestMode enum values
    std::vector<std::pair<int, int>> frameSizes;      // (columnCount, rowCount)
    okcli::CameraMode mode = okcli::CameraMode::Tpg;  // drives the exposure control's presentation
};

// How the exposure control should present itself. The number the GUI sends means different
// things per sensor — see CameraTypes.h: the AR0330 takes an exposure duration in milliseconds
// (a real integration time), while the OV5640 takes an AEC luminance target in 0..247 (a
// unitless brightness setpoint). A single label cannot honestly describe both, so the label and
// the readout follow the camera that is actually attached.
enum class ExposureUi {
    ShutterSpeed,  // AR0330: the stops are genuine shutter speeds, shown as 1/x
    AecTarget,     // OV5640: the value is a brightness setpoint, shown as its raw 0..247
    None           // no sensor (TPG); the control is disabled anyway
};

inline ExposureUi exposureUiFor(okcli::CameraMode m) {
    switch (m) {
        case okcli::CameraMode::SzgCam: return ExposureUi::ShutterSpeed;
        case okcli::CameraMode::Pcam:   return ExposureUi::AecTarget;
        case okcli::CameraMode::Tpg:    return ExposureUi::None;
    }
    return ExposureUi::None;
}

// Group-label text for each presentation.
inline const char* exposureTitleFor(ExposureUi ui) {
    switch (ui) {
        case ExposureUi::ShutterSpeed: return "Exposure (1/s)";
        case ExposureUi::AecTarget:    return "Brightness (AEC target)";
        case ExposureUi::None:         return "Exposure";
    }
    return "Exposure";
}

// ============================================================================================
// FPManager — FrontPanel device hot-plug monitor.
// ============================================================================================
class FPManager : public OpalKelly::FrontPanelManager {
public:
    using Notify = std::function<void(const std::string& serial, bool added)>;

    FPManager(wxEvtHandler* sink, Notify cb, const std::string& realm)
        : OpalKelly::FrontPanelManager(realm), m_sink(sink), m_cb(std::move(cb)) {}

    // Flush devices detected before processing started, and let future ones through.
    void StartDeviceProcessing() {  // GUI thread
        m_started = true;
        for (const auto& s : m_pending) m_cb(s, true);
        m_pending.clear();
    }

    // FrontPanelManager callbacks — may arrive on the monitor thread; marshal to the GUI thread so
    // m_pending/m_started/m_cb are only ever touched there (single-thread access).
    void OnDeviceAdded(const char* serial) override { dispatch(serial, true); }
    void OnDeviceRemoved(const char* serial) override { dispatch(serial, false); }

private:
    void dispatch(const std::string& serial, bool added) {
        auto run = [this, serial, added]() {  // runs on the GUI thread
            if (m_started) m_cb(serial, added);
            else if (added) m_pending.insert(serial);
            else m_pending.erase(serial);
        };
        if (wxIsMainThread()) run();
        else m_sink->CallAfter(run);
    }

    wxEvtHandler* m_sink;
    Notify m_cb;
    bool m_started = false;
    std::set<std::string> m_pending;
};

// ============================================================================================
// CameraWorker — runs camera operations on a dedicated thread (no enumeration; that's FPManager's job).
// ============================================================================================
class CameraWorker : public wxThread {
public:
    enum Result { SetupGood, SetupFail, CaptureGood, CaptureFail, CaptureShort, CaptureTimeout, Error,
                  DeviceProbed };
    using Action = std::function<void()>;

    // Opens a device by serial through the FrontPanelManager (the single open authority).
    using Opener = std::function<OpalKelly::FrontPanelPtr(const std::string&)>;

    CameraWorker(wxEvtHandler* sink, std::string bitfilesRoot, Opener opener)
        : wxThread(wxTHREAD_JOINABLE), m_sink(sink), m_bitfilesRoot(std::move(bitfilesRoot)),
          m_opener(std::move(opener)) {
        m_frame.rgb.reserve(static_cast<std::size_t>(2304) * 1296 * 3);  // fixed buffer, no realloc
    }

    void CallInCameraThread(Action a) {
        if (!IsMain()) throw std::runtime_error("Requests must be posted from the main thread.");
        m_requests.Post(std::move(a));
    }
    void Quit() {
        if (!IsMain()) throw std::runtime_error("Quit() must be called from the main thread.");
        m_requests.Post(Action());
    }
    void ClearCameraThreadQueue() { m_requests.Clear(); }

    // Probe a newly-detected device on the worker thread, so EVERY manager Open (probe + connect)
    // is serialized on one thread — the probe handle is fully closed before connect re-opens it.
    void RequestProbe(std::string serial) {
        CallInCameraThread([this, serial]() {
            CheckCalledInCameraThread();
            OpalKelly::FrontPanelPtr fp = m_opener(serial);
            GuiCameraBackend::DeviceEntry e;
            if (!fp || !GuiCameraBackend::describe(*fp, serial, e)) return;
            auto* ev = new wxThreadEvent;
            ev->SetInt(DeviceProbed);
            ev->SetPayload(e);
            wxQueueEvent(m_sink, ev);
        });
    }
    void RequestConnect(std::string serial, std::string realm, double exposure) {
        CallInCameraThread([this, serial, realm, exposure]() { Connect(serial, realm, exposure); });
    }
    void RequestSingleCapture() {
        CallInCameraThread([this]() { m_continuous = false; DoSingleCapture(); });
    }
    void RequestStartCapture() {
        CallInCameraThread([this]() { m_continuous = true; DoBufferedCapture(); });
    }
    void RequestStopCapture() { CallInCameraThread([this]() { m_continuous = false; }); }
    void RequestDisconnect() { CallInCameraThread([this]() { m_continuous = false; m_cam.close(); m_serial.clear(); }); }
    void RequestSetExposure(double e) {
        CallInCameraThread([this, e]() { CheckCalledInCameraThread(); std::string err; m_cam.setExposure(e, err); });
    }
    void RequestSetGains(int r, int g, int b) {
        CallInCameraThread([this, r, g, b]() { CheckCalledInCameraThread(); std::string err; m_cam.setGains(r, g, b, err); });
    }
    void RequestSetAWB(int v) {
        CallInCameraThread([this, v]() { CheckCalledInCameraThread(); std::string err; m_cam.setAWB(v, err); });
    }
    void RequestSetMotionSpeed(int v) {
        CallInCameraThread([this, v]() { CheckCalledInCameraThread(); std::string err; m_cam.setMotionSpeed(v, err); });
    }
    void RequestSetCameraMode(int testMode) {
        CallInCameraThread([this, testMode]() { CheckCalledInCameraThread(); std::string err; m_cam.setCameraMode(testMode, err); });
    }
    void RequestSetResolution(int index) {
        CallInCameraThread([this, index]() {
            CheckCalledInCameraThread();
            std::string err;
            if (!m_cam.setResolution(index, err)) PostError("resolution: " + err);
        });
    }
    void RequestRestart(double exp, int r, int g, int b, int awb, int testMode) {
        CallInCameraThread([this, exp, r, g, b, awb, testMode]() {
            CheckCalledInCameraThread();
            const bool wasContinuous = m_continuous;
            m_continuous = false;
            std::string err;
            if (!m_cam.restart(exp, r, g, b, awb, testMode, err)) { PostResult(SetupFail, err); return; }
            m_cam.warmup(2500, err);
            if (wasContinuous) { m_continuous = true; DoBufferedCapture(); }
        });
    }
    void RequestPipelineReset() {
        CallInCameraThread([this]() {
            CheckCalledInCameraThread();
            std::string err;
            if (!m_cam.resetPipeline(err)) PostError("pipeline reset: " + err);
        });
    }

    const GuiCameraBackend::Frame& CurrentFrame() const { return m_frame; }

protected:
    ExitCode Entry() override {
        for (;;) {
            Action action;
            wxMessageQueueError rc;
            if (m_continuous) rc = m_requests.ReceiveTimeout(0, action);
            else rc = m_requests.Receive(action);
            switch (rc) {
                case wxMSGQUEUE_NO_ERROR:
                    if (!action) { m_cam.close(); return nullptr; }  // empty action = exit
                    try { action(); } catch (const std::exception& e) { PostError(e.what()); }
                    break;
                case wxMSGQUEUE_TIMEOUT: DoBufferedCapture(); break;
                case wxMSGQUEUE_MISC_ERROR: PostError("Failed to read message"); break;
            }
        }
    }

    void Connect(const std::string& serial, const std::string& realm, double exposure) {
        CheckCalledInCameraThread();
        (void)realm;  // the FrontPanelManager (the opener) already binds the realm
        m_continuous = false;
        m_cam.close();  // release any previously-open device so a switch/re-open of one succeeds
        m_serial = serial;
        std::string err;
        OpalKelly::FrontPanelPtr fp = m_opener(serial);
        if (!fp) { PostResult(SetupFail, "could not open device " + serial); return; }
        // open() configures the FPGA and settles the hardware; startPipeline() brings up the sensor.
        if (!m_cam.open(std::move(fp), m_bitfilesRoot, "", err)) { PostResult(SetupFail, err); return; }
        // AWB gray-world threshold (0..255 = saturation threshold 0..1): 255 = use all pixels =
        // fully on, 0 = off. Default fully on so the FPGA white-balances the feed on every camera.
        if (!m_cam.startPipeline(exposure, 127, 127, 127, 255, -1, err)) { m_cam.close(); PostResult(SetupFail, err); return; }
        m_cam.warmup(2500, err);
        // SetupGood carries the device capabilities (payload), so the GUI can build the selectors.
        DeviceCaps caps;
        caps.name = m_cam.info().productName + " (" + okcli::cameraModeFriendly(m_cam.info().mode) + ")";
        caps.tpgOnly = m_cam.isTpgOnly();
        caps.mode = m_cam.info().mode;
        for (auto tm : m_cam.supportedTestModes()) caps.testModes.push_back(static_cast<int>(tm));
        for (const auto& fc : m_cam.supportedFrameConfigurations())
            caps.frameSizes.emplace_back(fc.dimensions.columnCount, fc.dimensions.rowCount);
        auto* e = new wxThreadEvent;
        e->SetInt(SetupGood);
        e->SetPayload(caps);
        wxQueueEvent(m_sink, e);
    }
    void DoSingleCapture() {
        CheckCalledInCameraThread();
        std::string err;
        if (m_cam.captureOnce(m_frame, err)) wxQueueEvent(m_sink, NewEvent(CaptureGood));
        else PostResult(CaptureFail, err);
    }
    void DoBufferedCapture() {
        CheckCalledInCameraThread();
        std::string err;
        if (m_cam.captureFrame(m_frame, err)) {
            auto* e = NewEvent(CaptureGood);
            e->SetExtraLong(0);  // missed-frame slot (the gateware exposes no count)
            wxQueueEvent(m_sink, e);
        } else { m_continuous = false; PostResult(CaptureFail, err); }
    }

private:
    wxThreadEvent* NewEvent(Result r) const {
        auto* e = new wxThreadEvent;  // wxEVT_THREAD
        e->SetInt(r);
        e->SetPayload(m_serial);  // device-id tag
        return e;
    }
    void PostError(const wxString& msg) { auto* e = NewEvent(Error); e->SetString(msg); wxQueueEvent(m_sink, e); }
    void PostResult(Result r, const wxString& msg) { auto* e = NewEvent(r); e->SetString(msg); wxQueueEvent(m_sink, e); }
    void CheckCalledInCameraThread() const {
        if (wxThread::GetCurrentId() != GetId())
            throw std::runtime_error("Camera functions must be called in the camera thread.");
    }

    wxEvtHandler* m_sink;
    std::string m_bitfilesRoot;
    Opener m_opener;
    GuiCameraBackend m_cam;
    GuiCameraBackend::Frame m_frame;
    wxMessageQueue<Action> m_requests;
    bool m_continuous = false;
    std::string m_serial;
};

// ============================================================================================
// HistogramPanel — RGB line histogram. Buffer is GBR order
// (channel 0 = Green, 1 = Blue, 2 = Red), 256 samples/channel.
// ============================================================================================
class HistogramPanel : public wxPanel {
public:
    explicit HistogramPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 170), wxFULL_REPAINT_ON_RESIZE) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(wxColour(24, 24, 24));
        Bind(wxEVT_PAINT, &HistogramPanel::OnPaint, this);
    }
    void UpdateHistogram(const std::vector<uint32_t>& hist) {
        m_hist = hist;
        Refresh(false);
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(wxColour(24, 24, 24)));
        dc.Clear();
        const wxSize sz = GetClientSize();
        if (sz.x <= 1 || sz.y <= 1) return;

        // Header strip: a "Histogram" title + an R/G/B legend, so the panel is clearly identifiable
        // (it's drawn even before the first frame, so an empty panel is still labelled).
        constexpr int kHeader = 22;
        wxFont font = GetFont();
        font.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(font);
        dc.SetTextForeground(wxColour(205, 205, 205));
        dc.DrawText("Histogram", 10, 4);
        int lx = 10 + dc.GetTextExtent("Histogram").GetWidth() + 18;
        const struct { const char* t; wxColour c; } legend[3] = {
            {"R", wxColour(0xEB, 0x57, 0x57)}, {"G", wxColour(0x44, 0xBD, 0x84)},
            {"B", wxColour(0x3B, 0x62, 0xDA)}};
        for (const auto& L : legend) {
            dc.SetTextForeground(L.c);
            dc.DrawText(L.t, lx, 4);
            lx += dc.GetTextExtent(L.t).GetWidth() + 12;
        }
        dc.SetPen(wxPen(wxColour(48, 48, 48), 1));  // subtle separator under the header
        dc.DrawLine(0, kHeader, sz.x, kHeader);

        constexpr int N = 256;
        if (static_cast<int>(m_hist.size()) < 3 * N) return;
        const int base = sz.y - 1;
        const int span = sz.y - kHeader - 2;  // draw curves below the header strip
        if (span < 2) return;
        uint32_t mx = 1;
        for (uint32_t v : m_hist) mx = std::max(mx, v);
        const wxColour cols[3] = {wxColour(0x44, 0xBD, 0x84), wxColour(0x3B, 0x62, 0xDA),
                                  wxColour(0xEB, 0x57, 0x57)};  // G, B, R (GBR order)
        for (int ch = 0; ch < 3; ++ch) {
            dc.SetPen(wxPen(cols[ch], 1));
            int px = 0, py = base;
            for (int i = 0; i < N; ++i) {
                const int x = (i * (sz.x - 1)) / (N - 1);
                const int y = base - static_cast<int>(
                                  (static_cast<double>(m_hist[ch * N + i]) / mx) * span);
                if (i > 0) dc.DrawLine(px, py, x, y);
                px = x;
                py = y;
            }
        }
    }
    std::vector<uint32_t> m_hist;
};

// ============================================================================================
// CameraFrame — the main window.
// ============================================================================================

// Defined in embedded_assets.cpp (generated): the GUI resources (camera.xrc + logo/icon images) are
// compiled into the executable and registered in an in-memory filesystem, so the UI and branding load
// with no dependency on the on-disk assets/ folder.
bool           LoadEmbeddedCameraXrc();
wxBitmap       EmbeddedBitmap(const wxString& name);
wxBitmapBundle EmbeddedSVG(const wxString& name, const wxSize& size);
wxIcon         EmbeddedIcon(const wxString& name);

class CameraFrame : public wxFrame {
public:
    // false if the UI resources failed to load; OnInit then skips Show() instead of presenting a
    // half-built window (or dying silently with no message).
    bool m_uiOk = false;

    // Preferred window size, clamped to the display's work area (the usable screen excluding any
    // system bars). On a large display this gives the roomy 1440x1048 floating window; on a smaller
    // display it shrinks to fit so the title bar and all content stay on screen. It remains an
    // ordinary resizable window, never forced to maximize or fullscreen.
    static wxSize fitToDisplay(int w, int h) {
        const wxRect work = wxGetClientDisplayRect();
        return wxSize(wxMin(w, work.GetWidth()), wxMin(h, work.GetHeight()));
    }

    CameraFrame()
        : wxFrame(nullptr, wxID_ANY, "Camera Example Design (C++ / FrontPanel-Platform)",
                  wxDefaultPosition, fitToDisplay(1440, 1048)) {
        // Clamp the minimum too, so it can't force the window larger than the display.
        SetMinSize(fitToDisplay(1100, 848));
        Centre();
        const char* root = std::getenv("OKCAM_BITFILES");
        m_bitfilesRoot = (root && *root) ? std::string(root) : bitfilesDir().ToStdString();

        buildUi();
        if (!m_uiOk) return;  // UI resources failed to load (buildUi reported it); bail before wiring up
        Bind(wxEVT_THREAD, &CameraFrame::OnCameraThread, this);
        Bind(wxEVT_CLOSE_WINDOW, &CameraFrame::OnClose, this);

#ifdef __APPLE__
        // macOS: wxOSX only builds the application ("Apple") menu — which owns the standard
        // "Quit okCameraApp" item and its Cmd-Q key equivalent — when a wxMenuBar exists. This app
        // has no menus of its own, so without one Cmd-Q has nothing to fire and is silently inert.
        // An EMPTY menu bar is exactly enough: wxMenuBar::Init() creates the Apple menu itself, and
        // MacInstallMenuBar() hides only About/Preferences when the app doesn't supply them, leaving
        // Quit live. No visible menu titles are added. Guarded because an empty menu bar on
        // Windows/GTK renders as a blank menu strip across the top of the window.
        SetMenuBar(new wxMenuBar());
#endif
        // Route Quit through the normal close path so OnClose() runs the teardown (stopBusy +
        // stopRealm: worker joined, device closed, hot-plug monitor stopped). Without this, wx's
        // fallback handler calls wxApp::ExitMainLoop() directly and wxEVT_CLOSE_WINDOW never fires
        // (wxWidgets #18328) — Cmd-Q would appear to work while leaking the device handle and both
        // threads. Unguarded and cross-platform-safe: with no menu bar on Windows/Linux, no
        // wxID_EXIT command can ever be generated.
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);

        // Start on the local USB realm. The FrontPanel-over-IP button switches to a remote realm.
        startRealm("");
    }

private:
    // Create the FrontPanel monitor + worker for a realm ("" = local USB, "fpoip://..." = remote).
    // The FrontPanelManager is the single device-open authority; the worker opens through it.
    void startRealm(const std::string& realm) {
        m_realm = realm;
        m_fpManager = new FPManager(this,
            [this](const std::string& serial, bool added) {
                if (added) onDeviceAdded(serial); else onDeviceRemoved(serial); },
            m_realm);
        m_worker = new CameraWorker(this, m_bitfilesRoot,
            [this](const std::string& serial) {
                return OpalKelly::FrontPanelPtr(m_fpManager->Open(serial.c_str())); });
        m_worker->Run();
        try {
            m_fpManager->StartMonitoring();       // throws if the connection fails to come up (e.g. FPoIP)
            m_fpManager->StartDeviceProcessing();
        } catch (const std::exception& e) {
            // A transient connect failure (e.g. an FPoIP network hiccup at bring-up) must not crash the
            // app. Tear the half-started realm down and report it; for a remote realm, fall back to
            // local USB so the app stays alive and the user can retry.
            stopRealm();
            if (!realm.empty()) {
                m_realm.clear();
                m_realmText->SetLabel("Realm: Local USB");
                updateFpoipButton();
                setStatus(wxString::Format("FrontPanel-over-IP connection failed (%s) — back on Local USB.",
                                           e.what()));
                startRealm("");                   // local USB monitoring does not touch the network
            } else {
                setStatus(wxString::Format("Device monitoring failed to start: %s", e.what()));
            }
        }
    }

    // Tear down the worker (fully joined) FIRST, then the monitor — so the worker's opener can never
    // touch a deleted manager. Safe to call repeatedly.
    void stopRealm() {
        CameraWorker* w = m_worker;
        m_worker = nullptr;  // OnCameraThread guards on this
        if (w) { w->ClearCameraThreadQueue(); w->Quit(); w->Wait(); delete w; }
        if (m_fpManager) { m_fpManager->StopMonitoring(); delete m_fpManager; m_fpManager = nullptr; }
    }

    static wxString exeDir() {
        return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    }

    // Where the staged data (camera_axi.lua, Bitfiles/) lives. On macOS the app is a .app bundle: the
    // executable is in Contents/MacOS and its data in Contents/Resources (Apple's layout, which also
    // lets the bundle code-sign cleanly). On Windows/Linux the data sits flat next to the executable.
    // This mirrors the staging split in gui/CMakeLists.txt.
    static wxString resourceDir() {
#ifdef __APPLE__
        return wxFileName(exeDir() + wxFILE_SEP_PATH + ".." + wxFILE_SEP_PATH + "Resources")
            .GetAbsolutePath();
#else
        return exeDir();
#endif
    }

    // Default bitfiles location: a "Bitfiles" folder in the resource dir (matches what the release
    // archive and the build both stage). OKCAM_BITFILES overrides this for custom-gateware builds.
    static wxString bitfilesDir() {
        return resourceDir() + wxFILE_SEP_PATH + "Bitfiles";
    }

    // The 36 discrete exposure stops. `label` is the shutter-speed DENOMINATOR and `value` is the
    // exposure in ms, so every stop satisfies value_ms == 1000 / denominator — the dial is a clean
    // 1/x sequence across its whole range.
    //
    // The last two stops used to be written "0\"3" and "0\"5" in photographic seconds notation
    // (0.3 s and 0.5 s). Those are exactly 1/3 s and 1/2 s, so they are now written as the
    // denominators 3 and 2 like every other stop. That makes the sequence uniform and lets the
    // readout render "1/x" for all 36 positions without special cases. The VALUES are unchanged —
    // this is purely notation, nothing different reaches the sensor.
    //
    // Default is index 24 ("30" → 33.3333 ms → displayed as 1/30).
    struct ExpStop { const char* label; double value; };
    static const std::vector<ExpStop>& exposureStops() {
        static const std::vector<ExpStop> s = {
            {"8000", 0.125}, {"6400", 0.15625}, {"5000", 0.2}, {"4000", 0.25}, {"3200", 0.3125},
            {"2500", 0.4}, {"2000", 0.5}, {"1600", 0.625}, {"1250", 0.8}, {"1000", 1}, {"800", 1.25},
            {"640", 1.5625}, {"500", 2}, {"400", 2.5}, {"320", 3.125}, {"250", 4}, {"200", 5},
            {"160", 6.25}, {"125", 8}, {"100", 10}, {"80", 12.5}, {"60", 16.6667}, {"50", 20},
            {"40", 25}, {"30", 33.3333}, {"25", 40}, {"20", 50}, {"15", 66.6667}, {"13", 76.9231},
            {"10", 100}, {"8", 125}, {"6", 166.6667}, {"5", 200}, {"4", 250}, {"3", 333.3333},
            {"2", 500}};
        return s;
    }
    static constexpr int kExposureDefaultIndex = 24;  // "30" → 1/30

    // Bind an XRC slider to update its value readout + invoke onChange.
    void wireSlider(wxSlider* s, wxStaticText* val, std::function<void(int)> onChange) {
        s->Bind(wxEVT_SLIDER, [s, val, onChange](wxCommandEvent&) {
            const int v = s->GetValue();
            if (val) val->SetLabel(wxString::Format("%d", v));
            onChange(v);
        });
    }

    // Build the branding header into the XRC placeholder: the lens logo (which doubles as the
    // spinning activity indicator — the busy frames replace it in place), an LED that lights during
    // capture, and the Opal Kelly wordmark, absolutely positioned as in the camera.xrc layout.
    void buildBrand(wxWindow* host) {
        m_logoBitmap = EmbeddedBitmap("logo/logo.png");
        for (int i = 1; i <= 39; ++i) {
            wxBitmap b = EmbeddedBitmap(wxString::Format("logo/busy%d.png", i));
            if (b.IsOk()) m_busyBitmaps.push_back(b);
        }
        m_logoBmp = new wxStaticBitmap(host, wxID_ANY,
                                       m_logoBitmap.IsOk() ? m_logoBitmap : wxNullBitmap, wxPoint(0, 20));
        wxBitmap led = EmbeddedBitmap("logo/led.png");
        m_ledBmp = new wxStaticBitmap(host, wxID_ANY, led.IsOk() ? led : wxNullBitmap, wxPoint(35, 47));
        m_ledBmp->Hide();
        wxBitmapBundle okmark = EmbeddedSVG("logo/opalkelly.svg", wxSize(150, 100));
        if (okmark.IsOk())
            new wxStaticBitmap(host, wxID_ANY, okmark.GetBitmap(wxSize(150, 100)), wxPoint(70, 0));
        m_busyTimer = new wxTimer(this);
        Bind(wxEVT_TIMER, &CameraFrame::OnBusyTick, this, m_busyTimer->GetId());
    }

    void buildUi() {
        // The version belongs in the title because on Linux there is nowhere else for it: that
        // archive ships only this GUI, ELF binaries have no file-properties equivalent of the
        // macOS Info.plist or a Windows VERSIONINFO resource, and okcameracli is not shipped.
        SetTitle("Opal Kelly - okCameraApp " OKCAM_VERSION);
        // Point the backend at camera_axi.lua (staged in the resource dir) for the server-side scripted
        // path over FPoIP. Only used when remote; local/TPG stay C++-direct. Env override wins.
        wxString luaEnv;
        if (!wxGetEnv("OKCAM_LUA", &luaEnv) || luaEnv.IsEmpty())
            wxSetEnv("OKCAM_LUA", resourceDir() + wxFILE_SEP_PATH + "camera_axi.lua");
#ifdef __WXMSW__
        { wxIcon ic = EmbeddedIcon("okApp.ico"); if (ic.IsOk()) SetIcon(ic); }
#endif
        // Load the sidebar/layout from the XRC. It is embedded in the executable (see embedded_xrc.cpp),
        // so the UI never depends on an external file - a missing or corrupt resource is the one failure
        // that would otherwise abort startup with no window and no message.
        wxXmlResource::Get()->InitAllHandlers();
        wxPanel* root = LoadEmbeddedCameraXrc()
                            ? wxXmlResource::Get()->LoadPanel(this, "root_panel")
                            : nullptr;
        if (!root) {
            wxMessageBox(
                "okCameraApp could not load its user-interface resources and cannot start.\n"
                "The application files may be incomplete or corrupted. Reinstall from the original archive.",
                "okCameraApp", wxOK | wxICON_ERROR);
            return;  // m_uiOk stays false; the constructor and OnInit both bail out cleanly
        }
        auto* frameSizer = new wxBoxSizer(wxVERTICAL);
        frameSizer->Add(root, 1, wxEXPAND);
        SetSizer(frameSizer);
        m_uiOk = true;

        // Bind the named XRC controls to our members.
        m_sidebar          = XRCCTRL(*this, "scrolled_sidebar", wxScrolledWindow);
        // Reserve room for the sidebar's vertical scrollbar. When the window is short enough that the
        // control column doesn't fit (e.g. on a smaller display), the sidebar scrolls vertically; a
        // gutter scrollbar would otherwise eat into the fixed sidebar width and clip the right edge of
        // the controls (the FPS readout, the choices, the sliders). Widen the sidebar by the
        // platform's scrollbar width so its content keeps the width the layout was designed for. On a
        // tall window no scrollbar appears, so this just leaves a few pixels of harmless extra width.
        {
            int sbw = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, m_sidebar);
            if (sbw <= 0) sbw = m_sidebar->FromDIP(16);  // macOS overlay scrollbars report 0
            wxSize sb = m_sidebar->GetMinSize();
            int base = sb.GetWidth() > 0 ? sb.GetWidth() : m_sidebar->FromDIP(300);
            m_sidebar->SetMinSize(wxSize(base + sbw, sb.GetHeight()));
        }
        m_realmText        = XRCCTRL(*this, "text_realm", wxStaticText);
        m_fpoipBtn         = XRCCTRL(*this, "btn_fpoip", wxButton);
        m_deviceChoice     = XRCCTRL(*this, "choice_devices", wxChoice);
        m_status           = XRCCTRL(*this, "text_status", wxStaticText);
        m_panelCamera      = XRCCTRL(*this, "panel_camera", wxPanel);
        m_camFps           = XRCCTRL(*this, "text_camera_fps", wxStaticText);
        m_sysFps           = XRCCTRL(*this, "text_system_fps", wxStaticText);
        m_continuousBtn    = XRCCTRL(*this, "btn_continuous", wxToggleButton);
        m_captureBtn       = XRCCTRL(*this, "btn_capture", wxButton);
        m_pipelineResetBtn = XRCCTRL(*this, "btn_restart", wxButton);
        m_modeChoice       = XRCCTRL(*this, "choice_mode", wxChoice);
        m_motionPanel      = XRCCTRL(*this, "panel_motion", wxPanel);
        m_motionSlider     = XRCCTRL(*this, "slider_motion", wxSlider);
        m_motionVal        = XRCCTRL(*this, "val_motion", wxStaticText);
        m_sizeChoice       = XRCCTRL(*this, "choice_size", wxChoice);
        m_imageSizeChoice  = XRCCTRL(*this, "choice_imagesize", wxChoice);
        m_histChk          = XRCCTRL(*this, "chk_hist", wxCheckBox);
        m_exposureSlider   = XRCCTRL(*this, "slider_exposure", wxSlider);
        m_exposureLabel    = XRCCTRL(*this, "val_exposure", wxStaticText);
        m_exposureTitle    = XRCCTRL(*this, "lbl_exposure", wxStaticText);
        m_awbSlider        = XRCCTRL(*this, "slider_awb", wxSlider);
        m_awbVal           = XRCCTRL(*this, "val_awb", wxStaticText);
        m_rGainSlider      = XRCCTRL(*this, "slider_rgain", wxSlider);
        m_rGainVal         = XRCCTRL(*this, "val_rgain", wxStaticText);
        m_gGainSlider      = XRCCTRL(*this, "slider_ggain", wxSlider);
        m_gGainVal         = XRCCTRL(*this, "val_ggain", wxStaticText);
        m_bGainSlider      = XRCCTRL(*this, "slider_bgain", wxSlider);
        m_bGainVal         = XRCCTRL(*this, "val_bgain", wxStaticText);

        // Long device labels must neither clip the controls nor be truncated (a wxChoice can't wrap).
        // Widen the fixed sidebar so the longest possible "board [peripheral]" (and the "Ready: ..."
        // status line) fits in full, measured in the control's own font + the dropdown arrow, padding and
        // scrollbar reserve. With the column sized to the widest label, the choices keep their natural
        // widths (no truncation of short ones like the Image Size "Scale"/"1:1"); the status/realm labels
        // ellipsize only as an edge-case safety (e.g. a very long FPoIP host), since the common strings fit.
        {
            const wxString widest = "Ready: SZG-HUB1450-AU10P (SZG-MIPI-8320)";  // longest board + peripheral
            const int need = m_deviceChoice->GetTextExtent(widest).GetWidth() + FromDIP(60);
            const wxSize sb = m_sidebar->GetMinSize();
            if (need > sb.GetWidth()) m_sidebar->SetMinSize(wxSize(need, sb.GetHeight()));
        }
        for (wxStaticText* t : {m_status, m_realmText}) {
            t->SetWindowStyleFlag(t->GetWindowStyleFlag() | wxST_ELLIPSIZE_END);
            t->SetMinSize(wxSize(FromDIP(60), -1));
        }

        // Fill the custom/animated placeholders (logo composite + image area) in code.
        buildBrand(XRCCTRL(*this, "brand_host", wxPanel));
        wxPanel* mainHost = XRCCTRL(*this, "main_host", wxPanel);
        auto* rightCol = new wxBoxSizer(wxVERTICAL);
        m_viewport = new RgbViewport(mainHost);
        rightCol->Add(m_viewport, 1, wxEXPAND);
        m_histPanel = new HistogramPanel(mainHost);
        rightCol->Add(m_histPanel, 0, wxEXPAND);
        mainHost->SetSizer(rightCol);
        m_histPanel->Show();  // histogram on by default (the "Show Histogram" box reflects it)

        m_continuousBtn->SetValue(true);
        m_histChk->SetValue(true);
        // Always enabled in local mode so a client with no local board can still start an FPoIP
        // connect; in remote mode it returns to local USB. Remote FPoIP support is checked on connect.
        m_fpoipBtn->Enable(true);

        // Wire handlers.
        m_deviceChoice->Bind(wxEVT_CHOICE, &CameraFrame::OnDeviceSelected, this);
        m_captureBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (m_ledBmp) m_ledBmp->Show();  // flash the LED for the single capture
            m_worker->RequestSingleCapture();
        });
        m_continuousBtn->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { setContinuous(m_continuousBtn->GetValue()); });
        m_pipelineResetBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_worker->RequestRestart(exposureStops()[m_exposureSlider->GetValue()].value,
                                     m_rGainSlider->GetValue(), m_gGainSlider->GetValue(),
                                     m_bGainSlider->GetValue(), m_awbSlider->GetValue(), currentMode());
        });
        m_modeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            m_worker->RequestSetCameraMode(currentMode());
            updateMotionVisibility();
        });
        m_sizeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            const int sel = m_sizeChoice->GetSelection();
            if (sel >= 0)
                m_worker->RequestSetResolution(
                    static_cast<int>(reinterpret_cast<intptr_t>(m_sizeChoice->GetClientData(sel))));
        });
        m_imageSizeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            m_viewport->SetScaleToFit(m_imageSizeChoice->GetSelection() == 0);
        });
        m_histChk->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            m_histPanel->Show(m_histChk->IsChecked());
            m_histPanel->GetParent()->Layout();
            m_viewport->Refresh();  // force a clean full repaint after the relayout
        });
        m_exposureSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
            const int i = m_exposureSlider->GetValue();
            m_exposureLabel->SetLabel(exposureReadout(i));
            m_worker->RequestSetExposure(exposureStops()[i].value);
        });
        // AWB slider value IS the gray-world saturation threshold written to the ISP register
        // (0..255 -> 0..1): 255 = all pixels used = fully on, 0 = off. No inversion.
        wireSlider(m_awbSlider, m_awbVal, [this](int v) { m_worker->RequestSetAWB(v); });
        wireSlider(m_rGainSlider, m_rGainVal, [this](int) { onGainChanged(); });
        wireSlider(m_gGainSlider, m_gGainVal, [this](int) { onGainChanged(); });
        wireSlider(m_bGainSlider, m_bGainVal, [this](int) { onGainChanged(); });
        wireSlider(m_motionSlider, m_motionVal, [this](int v) { m_worker->RequestSetMotionSpeed(v); });
        m_fpoipBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!m_realm.empty()) { switchToRealm(""); return; }  // already remote → back to local USB
            const wxString realm = promptFpoip();
            if (!realm.IsEmpty()) switchToRealm(std::string(realm.utf8_str()));
        });
    }

    // Reflect the current realm on the FPoIP button (connect vs. return to local).
    void updateFpoipButton() {
        m_fpoipBtn->SetLabel(m_realm.empty() ? "FrontPanel-over-IP..." : "Back to Local USB");
    }

    void enableCameraControls(bool on) {
        m_panelCamera->Show(on);  // show/hide the whole control panel
        m_panelCamera->Layout();
        m_sidebar->FitInside();
        m_sidebar->Layout();
    }
    void setStatus(const wxString& s) { m_status->SetLabel(s); }

    // Continuous (video) on/off. Single Capture is disabled while continuous is on.
    // The LED lights while capturing.
    void setContinuous(bool on) {
        m_continuousBtn->SetValue(on);
        m_captureBtn->Enable(!on);
        if (m_ledBmp) m_ledBmp->Show(on);
        if (on) m_worker->RequestStartCapture();
        else m_worker->RequestStopCapture();
    }

    void startBusy() {
        if (m_busyBitmaps.empty() || !m_busyTimer) return;
        m_busyFrame = 0;
        m_busyTimer->Start(40);
    }
    void stopBusy() {
        if (m_busyTimer) m_busyTimer->Stop();
        if (m_logoBmp && m_logoBitmap.IsOk()) m_logoBmp->SetBitmap(m_logoBitmap);  // restore static logo
    }
    void OnBusyTick(wxTimerEvent&) {
        if (m_busyBitmaps.empty() || !m_logoBmp) return;
        m_busyFrame = (m_busyFrame + 1) % static_cast<int>(m_busyBitmaps.size());
        m_logoBmp->SetBitmap(m_busyBitmaps[m_busyFrame]);
    }

    void onGainChanged() {
        m_worker->RequestSetGains(m_rGainSlider->GetValue(), m_gGainSlider->GetValue(),
                                  m_bGainSlider->GetValue());
    }

    static wxString testModeLabel(int v) {
        switch (static_cast<okcli::TestMode>(v)) {
            case okcli::TestMode::HorizontalRamp: return "Horizontal Ramp";
            case okcli::TestMode::VerticalRamp: return "Vertical Ramp";
            case okcli::TestMode::TemporalRamp: return "Temporal Ramp";
            case okcli::TestMode::SolidRed: return "Solid Red";
            case okcli::TestMode::SolidGreen: return "Solid Green";
            case okcli::TestMode::SolidBlue: return "Solid Blue";
            case okcli::TestMode::SolidBlack: return "Solid Black";
            case okcli::TestMode::SolidWhite: return "Solid White";
            case okcli::TestMode::CombinedRamp: return "Combined Ramp";
            case okcli::TestMode::Pseudorandom: return "Pseudorandom";
            case okcli::TestMode::DPColorRamp: return "DP Color Ramp";
            case okcli::TestMode::DPBWVertical: return "DP B/W Vertical";
            case okcli::TestMode::DPColorSquare: return "DP Color Square";
        }
        return "Unknown";
    }

    void populateCameraMode(const DeviceCaps& caps) {
        m_modeChoice->Clear();
        if (!caps.tpgOnly)
            m_modeChoice->Append("Image Capture", reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
        for (int tm : caps.testModes)
            m_modeChoice->Append(testModeLabel(tm), reinterpret_cast<void*>(static_cast<intptr_t>(tm)));
        if (m_modeChoice->GetCount() > 0) m_modeChoice->SetSelection(0);
        updateMotionVisibility();
    }

    // Motion Speed only applies to the moving ramp patterns (Horizontal/Vertical/Temporal ramp).
    void updateMotionVisibility() {
        const int m = currentMode();
        const bool show = (m == static_cast<int>(okcli::TestMode::HorizontalRamp) ||
                           m == static_cast<int>(okcli::TestMode::VerticalRamp) ||
                           m == static_cast<int>(okcli::TestMode::TemporalRamp));
        if (m_motionPanel && m_motionPanel->IsShown() != show) {
            m_motionPanel->Show(show);
            m_panelCamera->Layout();
            m_sidebar->FitInside();
            m_sidebar->Layout();
        }
    }

    // Render stop `i` the way the attached sensor actually interprets it.
    //
    //  ShutterSpeed (AR0330) — the value is a real integration time, so show the shutter speed
    //      as "1/x" using the stop's denominator label.
    //  AecTarget (OV5640) — the value is not a time at all; it is written to the AEC stable-range
    //      registers as a 0..247 luminance setpoint, clamped by PCAMCameraControl. Show the number
    //      that actually reaches the sensor, so the control does not claim a unit it does not have.
    //      Note this makes the clamping visible: the fastest stops all read 0 and the slowest all
    //      read 247, which is the pre-existing dead-zone defect tracked separately (R11.4) — this
    //      display change surfaces it rather than causing it.
    //  None (TPG) — no sensor; the control is disabled, so just show the raw stop.
    wxString exposureReadout(int i) const {
        const auto& stop = exposureStops()[i];
        switch (m_exposureUi) {
            case ExposureUi::ShutterSpeed:
                return wxString::Format("1/%s", stop.label);
            case ExposureUi::AecTarget:
                return wxString::Format("%d", std::max(0, std::min(247, static_cast<int>(stop.value))));
            case ExposureUi::None:
                break;
        }
        return wxString::Format("1/%s", stop.label);
    }

    // Point the exposure control at the sensor that is actually attached. Called on SetupGood.
    void applyExposureUi(okcli::CameraMode mode) {
        m_exposureUi = exposureUiFor(mode);
        if (m_exposureTitle) m_exposureTitle->SetLabel(exposureTitleFor(m_exposureUi));
        if (m_exposureLabel) m_exposureLabel->SetLabel(exposureReadout(m_exposureSlider->GetValue()));
        if (m_exposureTitle) m_exposureTitle->GetParent()->Layout();
    }

    void enableImageSettings(bool on) {  // disabled for TPG-only devices (no real sensor)
        m_exposureSlider->Enable(on);
        m_awbSlider->Enable(on);
        m_rGainSlider->Enable(on);
        m_gGainSlider->Enable(on);
        m_bGainSlider->Enable(on);
    }

    int currentMode() {  // selected Camera Mode (-1 = Image Capture), else a TestMode value
        const int sel = m_modeChoice->GetSelection();
        return (sel >= 0) ? static_cast<int>(reinterpret_cast<intptr_t>(m_modeChoice->GetClientData(sel)))
                          : -1;
    }

    void populateCaptureSize(const DeviceCaps& caps) {
        m_sizeChoice->Clear();
        for (std::size_t i = 0; i < caps.frameSizes.size(); ++i)
            m_sizeChoice->Append(
                wxString::Format("%dx%d", caps.frameSizes[i].first, caps.frameSizes[i].second),
                reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        if (m_sizeChoice->GetCount() > 0) m_sizeChoice->SetSelection(0);
    }

    // --- FrontPanelManager hot-plug callbacks (GUI thread) -----------------------------------
    void onDeviceAdded(const std::string& serial) {
        if (!m_worker) return;
        for (const auto& d : m_devices) if (d.serial == serial) return;  // already listed/queued
        m_worker->RequestProbe(serial);  // probe on the worker thread; result arrives as DeviceProbed
    }
    void onDeviceProbed(const GuiCameraBackend::DeviceEntry& e) {
        for (const auto& d : m_devices) if (d.serial == e.serial) return;  // de-dupe
        m_devices.push_back(e);
        rebuildDeviceChoice();
        if (!m_currentSerial.empty())  // a board arrived while another is active: list it, don't hijack
            setStatus(wxString::Format("%zu device(s) connected", m_devices.size()));
        makeDeviceActiveIfIdle(e.serial);  // connect the just-arrived device if idle (sets its own status)
    }
    void onDeviceRemoved(const std::string& serial) {
        for (std::size_t i = 0; i < m_devices.size(); ++i) {
            if (m_devices[i].serial == serial) { m_devices.erase(m_devices.begin() + i); break; }
        }
        if (serial == m_currentSerial) {  // the active device went away
            m_currentSerial.clear();
            m_worker->RequestDisconnect();
            enableCameraControls(false);
            m_viewport->ClearImage();
            m_deviceChoice->SetSelection(wxNOT_FOUND);
            setStatus("Camera connection lost");
        } else {
            setStatus(wxString::Format("Device %s disconnected", serial));
        }
        rebuildDeviceChoice();
    }

    void rebuildDeviceChoice() {
        const wxString keep = m_deviceChoice->GetStringSelection();
        m_deviceChoice->Clear();
        for (const auto& d : m_devices) {
            wxString label = d.productName;
            if (d.mode != okcli::CameraMode::Tpg)
                label += " [" + wxString(okcli::cameraModeFriendly(d.mode)) + "]";
            m_deviceChoice->Append(label);
        }
        if (!keep.empty()) m_deviceChoice->SetStringSelection(keep);
    }

    void OnDeviceSelected(wxCommandEvent&) {
        const int sel = m_deviceChoice->GetSelection();
        if (sel < 0 || sel >= static_cast<int>(m_devices.size())) return;
        connectDevice(m_devices[sel].serial);
    }

    // Select a device in the dropdown and bring it up. Shared by the manual dropdown handler and the
    // idle auto-connect (mirrors the RTL app's MakeDeviceActive/DoSelectDevice). The FPoIP button stays
    // enabled regardless, so a client with no local board can still reach FPoIP.
    void connectDevice(const std::string& serial) {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(m_devices.size()); ++i)
            if (m_devices[i].serial == serial) { idx = i; break; }
        if (idx < 0) return;  // device went away before we could connect
        m_deviceChoice->SetSelection(idx);
        m_currentSerial = serial;  // events tagged for other devices are now filtered as "late"
        enableCameraControls(false);
        setStatus("Connecting to " + wxString(m_devices[idx].productName) + " ...");
        startBusy();
        m_worker->RequestConnect(serial, m_realm, exposureStops()[m_exposureSlider->GetValue()].value);
    }

    // Mirror the RTL app's MakeDeviceActiveIfNecessary: bring up a freshly-arrived device only when nothing
    // is currently active, so a reconnect auto-connects like the first plug-in while a second board never
    // hijacks an active one.
    void makeDeviceActiveIfIdle(const std::string& serial) {
        if (m_currentSerial.empty()) connectDevice(serial);
    }

    void OnCameraThread(wxThreadEvent& evt) {
        if (!m_worker) return;
        const int r = evt.GetInt();
        if (r == CameraWorker::DeviceProbed) {  // payload is a DeviceEntry, not a serial — handle first
            onDeviceProbed(evt.GetPayload<GuiCameraBackend::DeviceEntry>());
            return;
        }
        if (r == CameraWorker::SetupGood) {  // not device-filtered
            stopBusy();
            const auto caps = evt.GetPayload<DeviceCaps>();
            setStatus("Ready: " + caps.name);
            populateCameraMode(caps);
            populateCaptureSize(caps);
            enableImageSettings(!caps.tpgOnly);
            applyExposureUi(caps.mode);
            enableCameraControls(true);
            // Every successful connect (first plug-in or a reconnect) starts in continuous/video, the
            // default view. An unplug's CaptureFail can leave the toggle off, so drive it on explicitly
            // via setContinuous() rather than trusting the toggle's current state.
            setContinuous(true);
            return;
        }
        // Late-event filter: ignore events tagged for a device we've switched away from.
        const std::string deviceId = evt.GetPayload<std::string>();
        if (deviceId != m_currentSerial) return;
        switch (r) {
            case CameraWorker::SetupFail:
                stopBusy();
                setStatus("Setup failed: " + evt.GetString());
                enableCameraControls(false);
                m_currentSerial.clear();
                break;
            case CameraWorker::CaptureGood: showFrame(); break;
            case CameraWorker::CaptureShort: setStatus("Capture readout short"); break;
            case CameraWorker::CaptureTimeout: setStatus("Capture timeout"); break;
            case CameraWorker::CaptureFail:
                setStatus("Capture failed: " + evt.GetString());
                m_continuousBtn->SetValue(false);
                m_captureBtn->Enable(true);
                break;
            case CameraWorker::Error: setStatus("Error: " + evt.GetString()); break;
        }
    }

    void showFrame() {
        const GuiCameraBackend::Frame& f = m_worker->CurrentFrame();
        m_viewport->UpdateImage(f.rgb.data(), f.width, f.height);
        if (m_histPanel->IsShown()) m_histPanel->UpdateHistogram(f.histogram);
        if (m_ledBmp && !m_continuousBtn->GetValue()) m_ledBmp->Hide();  // single-capture LED flash done
        ++m_frameCount;

        // System FPS = host pull rate; Camera FPS = unique sensor frames (frame.changed). Both are
        // sliding-window averages over the last 20 samples.
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch()).count();
        auto fpsOf = [](std::deque<long long>& ts) -> int {
            if (ts.size() < 2) return 0;
            const double dt = static_cast<double>(ts.back() - ts.front()) / (ts.size() - 1);
            return dt > 0 ? static_cast<int>(std::lround(1000.0 / dt)) : 0;
        };
        m_sysTs.push_back(now);
        if (m_sysTs.size() > 20) m_sysTs.pop_front();
        m_sysFps->SetLabel(wxString::Format("System FPS: %d", fpsOf(m_sysTs)));
        if (f.changed) {
            m_camTs.push_back(now);
            if (m_camTs.size() > 20) m_camTs.pop_front();
            m_camFps->SetLabel(wxString::Format("Camera FPS: %d", fpsOf(m_camTs)));
        }
    }

    void OnClose(wxCloseEvent& e) {
        stopBusy();
        stopRealm();
        e.Skip();
    }

    // Prompt for FrontPanel-over-IP connection details; returns a "fpoip://user:pass@host:port"
    // realm, or empty if cancelled / no host given.
    wxString promptFpoip() {
        wxDialog dlg(this, wxID_ANY, "Connect via FrontPanel-over-IP");
        auto* top = new wxBoxSizer(wxVERTICAL);
        auto* grid = new wxFlexGridSizer(2, 8, 10);
        grid->AddGrowableCol(1, 1);
        auto row = [&](const wxString& label, const wxString& def, long style) {
            grid->Add(new wxStaticText(&dlg, wxID_ANY, label), 0, wxALIGN_CENTRE_VERTICAL);
            auto* tc = new wxTextCtrl(&dlg, wxID_ANY, def, wxDefaultPosition, wxSize(220, -1), style);
            grid->Add(tc, 1, wxEXPAND);
            return tc;
        };
        wxTextCtrl* host = row("Host", "", 0);
        wxTextCtrl* port = row("Port", "9999", 0);
        wxTextCtrl* user = row("Username", "", 0);
        wxTextCtrl* pass = row("Password", "", wxTE_PASSWORD);
        top->Add(grid, 0, wxEXPAND | wxALL, 14);
        top->Add(dlg.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
        dlg.SetSizerAndFit(top);
        if (dlg.ShowModal() != wxID_OK || host->GetValue().IsEmpty()) return wxString();
        wxString realm = "fpoip://";
        if (!user->GetValue().IsEmpty()) {
            realm += user->GetValue();
            if (!pass->GetValue().IsEmpty()) realm += ":" + pass->GetValue();
            realm += "@";
        }
        realm += host->GetValue() + ":" + (port->GetValue().IsEmpty() ? "9999" : port->GetValue());
        return realm;
    }

    // Switch the active realm (local USB or FPoIP): tear the current one down and re-enumerate.
    void switchToRealm(const std::string& realm) {
        stopRealm();
        m_devices.clear();
        m_currentSerial.clear();
        m_deviceChoice->Clear();
        enableCameraControls(false);
        m_viewport->ClearImage();
        m_realmText->SetLabel("Realm: " + (realm.empty() ? wxString("Local USB") : wxString(realm)));
        setStatus(realm.empty() ? "Monitoring for devices..."
                                : wxString("Connecting to " + realm + " ..."));
        startRealm(realm);
        updateFpoipButton();
    }

    CameraWorker* m_worker = nullptr;
    FPManager* m_fpManager = nullptr;
    std::string m_realm;          // "" = local USB
    std::string m_bitfilesRoot;
    std::vector<GuiCameraBackend::DeviceEntry> m_devices;
    std::string m_currentSerial;
    wxScrolledWindow* m_sidebar = nullptr;
    wxPanel* m_panelCamera = nullptr;
    wxChoice* m_deviceChoice = nullptr;
    wxChoice* m_modeChoice = nullptr;
    wxChoice* m_sizeChoice = nullptr;
    wxChoice* m_imageSizeChoice = nullptr;
    wxCheckBox* m_histChk = nullptr;
    HistogramPanel* m_histPanel = nullptr;
    wxButton* m_fpoipBtn = nullptr;
    wxButton* m_captureBtn = nullptr;
    wxButton* m_pipelineResetBtn = nullptr;
    wxToggleButton* m_continuousBtn = nullptr;
    wxSlider* m_exposureSlider = nullptr;
    wxStaticText* m_exposureLabel = nullptr;
    wxStaticText* m_exposureTitle = nullptr;   // group label; text follows the attached sensor
    ExposureUi m_exposureUi = ExposureUi::None;
    wxSlider* m_awbSlider = nullptr;
    wxStaticText* m_awbVal = nullptr;
    wxPanel* m_motionPanel = nullptr;
    wxSlider* m_motionSlider = nullptr;
    wxStaticText* m_motionVal = nullptr;
    wxSlider* m_rGainSlider = nullptr;
    wxStaticText* m_rGainVal = nullptr;
    wxSlider* m_gGainSlider = nullptr;
    wxStaticText* m_gGainVal = nullptr;
    wxSlider* m_bGainSlider = nullptr;
    wxStaticText* m_bGainVal = nullptr;
    wxStaticText* m_realmText = nullptr;
    wxStaticText* m_status = nullptr;
    wxStaticText* m_camFps = nullptr;
    wxStaticText* m_sysFps = nullptr;
    std::deque<long long> m_sysTs, m_camTs;
    RgbViewport* m_viewport = nullptr;
    wxStaticBitmap* m_logoBmp = nullptr;
    wxStaticBitmap* m_ledBmp = nullptr;
    wxBitmap m_logoBitmap;
    std::vector<wxBitmap> m_busyBitmaps;
    wxTimer* m_busyTimer = nullptr;
    int m_busyFrame = 0;
    int m_frameCount = 0;
};

class CameraGuiApp : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        auto* frame = new CameraFrame();
        if (!frame->m_uiOk) { frame->Destroy(); return false; }
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(CameraGuiApp);
