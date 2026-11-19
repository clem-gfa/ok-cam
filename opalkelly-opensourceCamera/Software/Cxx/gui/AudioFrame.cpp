/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "AudioFrame.h"

#include <algorithm>
#include <chrono>
#include <ctime>

#include <wx/dcbuffer.h>

using namespace okcli;

namespace {
constexpr int kFullScale = 8388608;          // 2^23
const wxColour kBackground(16, 20, 24);
const wxColour kGridLine(42, 50, 58);
const wxColour kTrace(90, 209, 160);
const wxColour kLabel(125, 135, 144);
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
AudioWaveCanvas::AudioWaveCanvas(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(640, 448), wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(320, 224));
    Bind(wxEVT_PAINT, &AudioWaveCanvas::OnPaint, this);
}

void AudioWaveCanvas::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    DrawTo(dc, GetClientSize());
}

void AudioWaveCanvas::DrawTo(wxDC& dc, const wxSize& size) {
    dc.SetBackground(wxBrush(kBackground));
    dc.Clear();
    const int cols = (m_cols >= 1 && kAudioChannels % m_cols == 0) ? m_cols : 8;
    const int rows = kAudioChannels / cols;
    const double cw = double(size.GetWidth()) / cols;
    const double chh = double(size.GetHeight()) / rows;
    const std::size_t blocks = m_audio
        ? std::max<std::size_t>(1, std::size_t((double(m_windowMs) / 1000.0 * m_audio->sampleRate()) / kAudioPeakBlock + 0.5))
        : 1;
    const double k = ((chh / 2 - 2) * m_gain) / kFullScale;
    const int pxCols = std::max(8, int(cw) - 2);
    // all channels at once: min/max per pixel column of the last `blocks` blocks (one pass over the peaks, see windowPeaks)
    std::size_t n = 0;
    if (m_audio) {
        const std::size_t need = std::size_t(kAudioChannels) * std::size_t(pxCols);
        if (m_min.size() < need) { m_min.resize(need); m_max.resize(need); }
        n = m_audio->windowPeaks(blocks, std::size_t(pxCols), m_min.data(), m_max.data());
    }
    dc.SetFont(wxFont(7, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    for (int ch = 0; ch < kAudioChannels; ch++) {
        const int x0 = int((ch % cols) * cw);
        const int y0 = int((ch / cols) * chh);
        const int w = int(cw), h = int(chh);
        const double mid = y0 + chh / 2;
        dc.SetPen(wxPen(kGridLine));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(x0, y0, w, h);
        if (m_audio) {
            if (n > 0) {
                dc.SetPen(wxPen(kTrace));
                const int32_t* cmin = &m_min[std::size_t(ch) * std::size_t(pxCols)];
                const int32_t* cmax = &m_max[std::size_t(ch) * std::size_t(pxCols)];
                for (int px = 0; px < pxCols; px++) {
                    const int32_t mn = cmin[px], mx = cmax[px];
                    const int yTop = int(std::max<double>(y0 + 1, mid - mx * k));
                    const int yBot = int(std::min<double>(y0 + h - 1, mid - mn * k));
                    dc.DrawLine(x0 + 1 + px, yTop, x0 + 1 + px, std::max(yBot, yTop + 1));
                }
            }
        }
        dc.SetTextForeground(kLabel);
        dc.DrawText(wxString::Format("%d", ch + 1), x0 + 3, y0 + 1);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
AudioFrame::AudioFrame(wxWindow* parent, Actions actions, std::shared_ptr<okcli::RecordingSettings> rec)
    : wxFrame(parent, wxID_ANY, "Microphones (64 channels)", wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
      m_actions(std::move(actions)), m_rec(rec ? std::move(rec) : std::make_shared<okcli::RecordingSettings>()), m_timer(this) {
    auto* root = new wxPanel(this);
    auto* col = new wxBoxSizer(wxVERTICAL);

    auto* bar = new wxBoxSizer(wxHORIZONTAL);
    m_startBtn = new wxButton(root, wxID_ANY, "Start microphones");
    m_recBtn = new wxButton(root, wxID_ANY, "Record");
    m_recBtn->Enable(false);
    bar->Add(m_startBtn, 0, wxRIGHT, 6);
    bar->Add(m_recBtn, 0, wxRIGHT, 6);
    bar->Add(new wxStaticText(root, wxID_ANY, "File prefix"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
    m_prefix = new wxTextCtrl(root, wxID_ANY, wxString::FromUTF8(m_rec->prefix), wxDefaultPosition, wxSize(110, -1));
    m_prefix->SetToolTip("Start of the recording file names: <prefix>_audio_<date>_<time>_NNN.wav");
    bar->Add(m_prefix, 0, wxRIGHT, 12);
    bar->Add(new wxStaticText(root, wxID_ANY, "Window"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
    m_window = new wxChoice(root, wxID_ANY);
    for (const char* s : {"2 ms", "5 ms", "10 ms", "20 ms", "50 ms", "100 ms", "200 ms", "500 ms", "1 s", "2 s"}) m_window->Append(s);
    m_window->SetSelection(3);
    m_window->SetToolTip("Time span shown by each waveform (the display keeps the min / max of blocks of 8 samples = 42 us)");
    bar->Add(m_window, 0, wxRIGHT, 10);
    bar->Add(new wxStaticText(root, wxID_ANY, "Gain"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
    m_gain = new wxChoice(root, wxID_ANY);
    for (const char* s : {"x1", "x2", "x4", "x8", "x16", "x32", "x64", "x128", "x256", "x512"}) m_gain->Append(s);
    m_gain->SetSelection(3);
    bar->Add(m_gain, 0, wxRIGHT, 10);
    bar->Add(new wxStaticText(root, wxID_ANY, "Layout"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
    m_layout = new wxChoice(root, wxID_ANY);
    for (const char* s : {"8 x 8", "16 x 4", "4 x 16", "32 x 2", "2 x 32"}) m_layout->Append(s);
    m_layout->SetToolTip("Waveforms per row x rows");
    m_layout->SetSelection(0);
    bar->Add(m_layout, 0);
    col->Add(bar, 0, wxALL, 8);

    // Size of the recorded files: lower sample rate (decimation with an anti-aliasing filter) and / or 16 bits
    auto* bar2 = new wxBoxSizer(wxHORIZONTAL);
    bar2->Add(new wxStaticText(root, wxID_ANY, "Recorded files:  sample rate"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
    m_recRate = new wxChoice(root, wxID_ANY);
    for (const char* s : {"full (189 kS/s)", "/ 2 (94.7 kS/s)", "/ 4 (47.3 kS/s)", "/ 8 (23.7 kS/s)"}) m_recRate->Append(s);
    bar2->Add(m_recRate, 0, wxRIGHT, 10);
    bar2->Add(new wxStaticText(root, wxID_ANY, "bits"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
    m_recBits = new wxChoice(root, wxID_ANY);
    for (const char* s : {"24", "16"}) m_recBits->Append(s);
    bar2->Add(m_recBits, 0, wxRIGHT, 10);
    m_recInfo = new wxStaticText(root, wxID_ANY, "");
    bar2->Add(m_recInfo, 0, wxALIGN_CENTRE_VERTICAL);
    col->Add(bar2, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_canvas = new AudioWaveCanvas(root);
    col->Add(m_canvas, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    m_stats = new wxStaticText(root, wxID_ANY, "");
    m_hw = new wxStaticText(root, wxID_ANY, "");
    m_msg = new wxStaticText(root, wxID_ANY, "");
    m_msg->SetForegroundColour(wxColour(200, 120, 20));
    col->Add(m_stats, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    col->Add(m_hw, 0, wxLEFT | wxRIGHT, 8);
    col->Add(m_msg, 0, wxALL, 8);
    root->SetSizer(col);
    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(root, 1, wxEXPAND);
    SetSizerAndFit(top);
    SetMinSize(wxSize(520, 420));

    m_startBtn->Bind(wxEVT_BUTTON, &AudioFrame::OnStartStop, this);
    m_recBtn->Bind(wxEVT_BUTTON, &AudioFrame::OnRecord, this);
    auto recFormatChanged = [this](wxCommandEvent&) {
        static const int kDec[] = {1, 2, 4, 8};
        const int i = m_recRate->GetSelection();
        m_rec->setAudioFormat(kDec[(i >= 0 && i < 4) ? i : 0], m_recBits->GetSelection() == 1 ? 16 : 24);
    };
    m_recRate->Bind(wxEVT_CHOICE, recFormatChanged);
    m_recBits->Bind(wxEVT_CHOICE, recFormatChanged);
    ShowRecFormat();
    m_rec->listeners.push_back([this]() { ShowRecFormat(); });
    m_prefix->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { m_rec->setPrefix(m_prefix->GetValue().ToStdString(wxConvUTF8)); });
    m_rec->listeners.push_back([this]() {
        const wxString v = wxString::FromUTF8(m_rec->prefix);
        if (m_prefix->GetValue() != v) m_prefix->ChangeValue(v);
    });
    for (wxChoice* c : {m_window, m_gain, m_layout}) c->Bind(wxEVT_CHOICE, &AudioFrame::OnSettings, this);
    Bind(wxEVT_TIMER, &AudioFrame::OnTimer, this);
    Bind(wxEVT_SHOW, &AudioFrame::OnShow, this);
    Bind(wxEVT_CLOSE_WINDOW, &AudioFrame::OnCloseWindow, this);
    ApplySettings();
}

void AudioFrame::OnCloseWindow(wxCloseEvent& e) {
    // Hide instead of destroying: the acquisition (and a recording) keeps running while the window is closed.
    if (e.CanVeto()) { e.Veto(); Hide(); }
    else Destroy();
}

void AudioFrame::OnShow(wxShowEvent& e) {
    if (e.IsShown()) m_timer.Start(50);   // 20 images/s, only while visible
    else m_timer.Stop();
    e.Skip();
}

void AudioFrame::ApplySettings() {
    static const int windows[] = {2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000};
    static const int gains[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
    static const int cols[] = {8, 16, 4, 32, 2};
    auto pick = [](const int* table, std::size_t size, int sel, int def) { return (sel >= 0 && std::size_t(sel) < size) ? table[sel] : def; };
    m_canvas->SetWindowMs(pick(windows, 10, m_window->GetSelection(), 20));
    m_canvas->SetGain(pick(gains, 10, m_gain->GetSelection(), 8));
    m_canvas->SetCols(pick(cols, 5, m_layout->GetSelection(), 8));
    m_canvas->Refresh(false);
}

void AudioFrame::OnSettings(wxCommandEvent&) { ApplySettings(); }

void AudioFrame::OnStartStop(wxCommandEvent&) {
    if (!m_running) {
        m_starting = true;
        m_startBtn->Enable(false);
        m_msg->SetLabel("Starting microphones (clock sequence ~35 ms)...");
        if (m_actions.start) m_actions.start();
    } else {
        m_startBtn->Enable(false);
        if (m_actions.stop) m_actions.stop();
    }
}

AudioRecordFormat AudioFrame::SelectedFormat() const {
    AudioRecordFormat f;
    f.decimation = m_rec->audioDecimation;
    f.bits = m_rec->audioBits;
    return f.valid() ? f : AudioRecordFormat();
}

// The two choices + the estimated data rate of the files
void AudioFrame::ShowRecFormat() {
    const AudioRecordFormat f = SelectedFormat();
    m_recRate->SetSelection(f.decimation == 2 ? 1 : f.decimation == 4 ? 2 : f.decimation == 8 ? 3 : 0);
    m_recBits->SetSelection(f.bits == 16 ? 1 : 0);
    const double bps = f.bytesPerSecond(kAudioTickHz / kAudioTicksPerFrame);
    m_recInfo->SetLabel(wxString::Format("= %.1f MB/s, %.1f GB per hour", bps / 1e6, bps * 3600 / 1e9));
}

bool AudioFrame::StartRecordingWith(const std::string& dir, const std::string& session, std::string& why) {
    if (!m_audio) { why = "the microphones are not running"; return false; }
    const AudioRecordFormat fmt = SelectedFormat();
    if (!audioDiskSpaceOk(dir, 60, why, fmt.bytesPerSecond(m_audio->sampleRate()))) return false;
    if (m_rec->persist) m_rec->persist();
    m_audio->startRecording(dir, session, defaultAudioSinkFactory(), fmt);
    m_msg->SetLabel("");
    UpdateButtons();
    return true;
}

void AudioFrame::StopRecording() {
    if (m_audio && m_audio->recording()) m_audio->stopRecording();   // waits for the pending writes
    UpdateButtons();
}

void AudioFrame::OnRecord(wxCommandEvent&) {
    if (!m_audio) return;
    if (m_audio->recording()) {
        StopRecording();
        return;
    }
    wxDirDialog dlg(this, "Folder for the audio recordings", m_rec->dir.empty() ? wxGetHomeDir() : wxString::FromUTF8(m_rec->dir));
    if (dlg.ShowModal() != wxID_OK) return;
    m_rec->setDir(dlg.GetPath().ToStdString(wxConvUTF8));
    const std::string session = okcli::recordingSession(m_rec->prefix, true, std::time(nullptr));
    std::string why;
    if (!StartRecordingWith(m_rec->dir, session, why)) m_msg->SetLabel(wxString::FromUTF8(why));
}

void AudioFrame::OnAudioState(bool running, std::shared_ptr<AudioCapture> audio, const wxString& message) {
    m_running = running;
    m_starting = false;
    if (audio) { m_audio = std::move(audio); m_canvas->SetAudio(m_audio); }
    m_msg->SetLabel(message);
    UpdateButtons();
}

void AudioFrame::SetCounters(const MicCounters& c) {
    m_counters = c;
    m_haveCounters = true;
}

void AudioFrame::UpdateButtons() {
    m_startBtn->SetLabel(m_running ? "Stop" : "Start microphones");
    m_startBtn->Enable(!m_starting);
    const bool rec = m_audio && m_audio->recording();
    m_recBtn->SetLabel(rec ? "Stop recording" : "Record");
    m_recBtn->Enable(m_running && m_audio);
    m_recRate->Enable(!rec);          // the format of a recording in progress cannot change
    m_recBits->Enable(!rec);
}

wxString AudioFrame::FormatStats(const AudioStats& s, uint32_t /*sampleRate*/) {
    const double lostPct = 100.0 * double(s.lostPackets) / std::max(1.0, double(s.packets + s.lostPackets));
    wxString t = wxString::Format("packets %llu | lost %llu (%.1f%%) | bad %llu | host gap max %.1f ms, %llu over 3 ms",
                                  (unsigned long long)s.packets, (unsigned long long)s.lostPackets, lostPct,
                                  (unsigned long long)s.badBlocks, s.maxGapMs, (unsigned long long)s.longGaps);
    if (s.elapsedMs > 1000) {
        t += wxString::Format(" | busy: pipe %.0f%%, decode %.0f%%, rate %.1f MB/s (need 36)", 100.0 * s.readMs / s.elapsedMs,
                              100.0 * s.decodeMs / s.elapsedMs, double(s.packets) * kAudioPacketBytes / 1e6 / (s.elapsedMs / 1000.0));
    }
    if (s.skippedPackets > 0) t += wxString::Format(" | skipped %llu (catching up)", (unsigned long long)s.skippedPackets);
    return t;
}

wxString AudioFrame::FormatCounters(const MicCounters& c, uint32_t sampleRate) {
    wxString t = wxString::Format("FPGA: dropped %u | overruns %u | FIFO %.0f KB |%s", c.dropped, c.overruns,
                                  c.level * 4.0 / 1024.0, (c.status & kMicStatusFull) ? " FIFO/ring was full" : " never full");
    if (c.ringPeak > 0 && sampleRate > 0)
        t += wxString::Format(" | DDR4 ring: %u pk now, peak %u pk = %.0f ms of backlog", c.ringUsed, c.ringPeak,
                              double(c.ringPeak) * kAudioPacketBytes / (double(kAudioFrameBytes) * sampleRate) * 1000.0);
    return t;
}

void AudioFrame::OnTimer(wxTimerEvent&) {
    m_canvas->Refresh(false);
    if (!m_audio) return;
    const AudioStats st = m_audio->stats();
    wxString line = FormatStats(st, m_audio->sampleRate());
    if (m_audio->recording() || st.segments > 0)
        line += wxString::Format(" | rec %.1f s, %llu segment(s) written", double(st.recordedFrames) / m_audio->sampleRate(),
                                 (unsigned long long)st.segments);
    m_stats->SetLabel(line);
    if (m_haveCounters) m_hw->SetLabel(FormatCounters(m_counters, m_audio->sampleRate()));
    const std::string recErr = m_audio->recordError();
    if (!recErr.empty()) m_msg->SetLabel("Recording stopped: " + wxString::FromUTF8(recErr));
    UpdateButtons();
}
