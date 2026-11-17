/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// AudioFrame.h — floating window for the 64 PDM microphones (XEM8310 / DARWIN): start/stop the microphones, 64 sliding
// waveforms (8 x 8 or 16 x 4 grid, min/max envelope per pixel column), recording to numbered segments.
// Counterpart of Software/FP-Platform/src/AudioPanel.tsx.
//
// No dependency on the FrontPanel SDK: it reads the thread-safe AudioCapture (statistics, peaks, recording), and asks the
// camera worker thread to start/stop the microphones through callbacks. All methods are for the GUI (main) thread.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <wx/wx.h>

#include "AudioStream.h"

class AudioWaveCanvas : public wxPanel {
public:
    explicit AudioWaveCanvas(wxWindow* parent);

    void SetAudio(std::shared_ptr<okcli::AudioCapture> audio) { m_audio = std::move(audio); Refresh(false); }
    void SetWindowMs(int ms) { m_windowMs = ms; }
    void SetGain(int gain) { m_gain = gain; }
    void SetWide(bool wide) { m_wide = wide; }          // 16 x 4 instead of 8 x 8
    // Draws the grid into any DC (used by OnPaint, and by tests on a memory DC).
    void DrawTo(wxDC& dc, const wxSize& size);

private:
    void OnPaint(wxPaintEvent&);

    std::shared_ptr<okcli::AudioCapture> m_audio;
    int m_windowMs = 20, m_gain = 8;
    bool m_wide = false;
    std::vector<int32_t> m_min, m_max;                  // reused buffers (no allocation per image)
};

class AudioFrame : public wxFrame {
public:
    struct Actions {
        std::function<void()> start;    // ask the camera worker to reset + enable the microphones
        std::function<void()> stop;     // ask the camera worker to stop a recording and disable them
    };
    AudioFrame(wxWindow* parent, Actions actions);

    // Worker -> GUI results (call from the main thread, e.g. from the wxThreadEvent handler)
    void OnAudioState(bool running, std::shared_ptr<okcli::AudioCapture> audio, const wxString& message);
    void SetCounters(const okcli::MicCounters& c);       // WireOut counters, polled about once a second by the worker
    // Statistics line as shown under the waveforms (also used by tests)
    static wxString FormatStats(const okcli::AudioStats& s, uint32_t sampleRate);
    static wxString FormatCounters(const okcli::MicCounters& c, uint32_t sampleRate);

    AudioWaveCanvas* Canvas() { return m_canvas; }

private:
    void OnStartStop(wxCommandEvent&);
    void OnRecord(wxCommandEvent&);
    void OnSettings(wxCommandEvent&);
    void OnTimer(wxTimerEvent&);
    void OnShow(wxShowEvent&);
    void OnCloseWindow(wxCloseEvent& e);
    void UpdateButtons();
    void ApplySettings();

    Actions m_actions;
    std::shared_ptr<okcli::AudioCapture> m_audio;
    bool m_running = false;
    bool m_starting = false;
    std::string m_recDir;
    okcli::MicCounters m_counters;
    bool m_haveCounters = false;

    wxButton* m_startBtn = nullptr;
    wxButton* m_recBtn = nullptr;
    wxChoice* m_window = nullptr;
    wxChoice* m_gain = nullptr;
    wxChoice* m_layout = nullptr;
    AudioWaveCanvas* m_canvas = nullptr;
    wxStaticText* m_stats = nullptr;
    wxStaticText* m_hw = nullptr;
    wxStaticText* m_msg = nullptr;
    wxTimer m_timer;
};
