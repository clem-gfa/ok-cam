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

#include "RecordingName.h"
#include <wx/wx.h>

#include "AudioStream.h"

class AudioWaveCanvas : public wxPanel {
public:
    explicit AudioWaveCanvas(wxWindow* parent);

    void SetAudio(std::shared_ptr<okcli::AudioCapture> audio) { m_audio = std::move(audio); Refresh(false); }
    void SetWindowMs(int ms) { m_windowMs = ms; }
    void SetGain(int gain) { m_gain = gain; }
    void SetWide(bool wide) { m_cols = wide ? 16 : 8; } // 16 x 4 instead of 8 x 8
    void SetCols(int cols) { m_cols = std::max(1, std::min(64, cols)); }   // columns of the grid (rows = 64 / cols)
    // Draws the grid into any DC (used by OnPaint, and by tests on a memory DC).
    void DrawTo(wxDC& dc, const wxSize& size);

private:
    void OnPaint(wxPaintEvent&);

    std::shared_ptr<okcli::AudioCapture> m_audio;
    int m_windowMs = 20, m_gain = 8;
    int m_cols = 8;
    std::vector<int32_t> m_min, m_max;                  // reused buffers (no allocation per image)
};

class AudioFrame : public wxFrame {
public:
    struct Actions {
        std::function<void()> start;    // ask the camera worker to reset + enable the microphones
        std::function<void()> stop;     // ask the camera worker to stop a recording and disable them
    };
    // `rec`: prefix + folder of the recordings, shared with the main window (a private one is created when null)
    AudioFrame(wxWindow* parent, Actions actions, std::shared_ptr<okcli::RecordingSettings> rec = nullptr);

    // Worker -> GUI results (call from the main thread, e.g. from the wxThreadEvent handler)
    void OnAudioState(bool running, std::shared_ptr<okcli::AudioCapture> audio, const wxString& message);
    void SetCounters(const okcli::MicCounters& c);       // WireOut counters, polled about once a second by the worker
    // Statistics line as shown under the waveforms (also used by tests)
    static wxString FormatStats(const okcli::AudioStats& s, uint32_t sampleRate);
    static wxString FormatCounters(const okcli::MicCounters& c, uint32_t sampleRate);

    AudioWaveCanvas* Canvas() { return m_canvas; }

    // Used by the video recording: the audio is recorded with the same folder + session name (<session>_NNN.wav / .csv next
    // to <session>_video.*), so that the files belong together.
    bool IsRunning() const { return m_running && m_audio; }
    std::shared_ptr<okcli::AudioCapture> Audio() const { return m_audio; }   // for the acoustic camera (beamforming)
    bool IsRecording() const { return m_audio && m_audio->recording(); }
    bool StartRecordingWith(const std::string& dir, const std::string& session, std::string& why);
    void StopRecording();
    okcli::AudioRecordFormat SelectedFormat() const;   // sample rate / bits chosen for the recorded files

private:
    void ShowRecFormat();
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
    std::shared_ptr<okcli::RecordingSettings> m_rec;
    wxTextCtrl* m_prefix = nullptr;
    wxChoice* m_recRate = nullptr;
    wxChoice* m_recBits = nullptr;
    wxStaticText* m_recInfo = nullptr;
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
