// Test de fumee des fenetres wx (AudioFrame, UserIoFrame) sans SDK ni materiel : dessin des 64 formes d'onde dans une
// image memoire (verification des pixels), textes de statistiques, mode simule des I/O.
// Build + run (affichage virtuel) :
//   g++ -std=c++17 -Isrc gui/AudioFrame.cpp gui/UserIoFrame.cpp tests/gui_smoke_test.cpp $(wx-config --cxxflags --libs) -pthread
//   xvfb-run -a ./a.out
#include <cassert>
#include <cmath>
#include <deque>
#include <iostream>

#include <wx/dcmemory.h>
#include <wx/wx.h>

#include "../gui/AudioFrame.h"
#include "../gui/UserIoFrame.h"

using namespace okcli;

// a sinusoid per channel, 24-bit, packed like the gateware does
static std::vector<uint8_t> makePacket(uint32_t seq, uint64_t& frameCounter) {
    std::vector<uint8_t> p(kAudioPacketBytes, 0);
    auto put32 = [&](int w, uint32_t x) { for (int i = 0; i < 4; i++) p[4 * w + i] = uint8_t(x >> (8 * i)); };
    put32(0, kAudioMagic); put32(1, seq); put32(4, 21);
    for (int i = 0; i < 21; i++, frameCounter++)
        for (int c = 0; c < 64; c++) {
            const double a = 2000000.0 * std::sin(2 * M_PI * (1000.0 + 700.0 * c) * double(frameCounter) / 189318.0);
            const uint32_t v = uint32_t(int32_t(a)) & 0xFFFFFF;
            uint8_t* d = &p[32 + i * 192 + c * 3];
            d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); d[2] = uint8_t(v >> 16);
        }
    return p;
}

class FakePipe : public IAudioPipe {
public:
    std::deque<std::vector<uint8_t>> q;
    uint32_t available() override { return uint32_t(q.size() * kAudioPacketBytes / 4); }
    void read(uint8_t* b, std::size_t n) override {
        for (std::size_t i = 0; i < n / kAudioPacketBytes; i++) { std::memcpy(b + i * kAudioPacketBytes, q.front().data(), kAudioPacketBytes); q.pop_front(); }
    }
};

class App : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(App);

int main(int argc, char** argv) {
    wxEntryStart(argc, argv);
    wxTheApp->CallOnInit();
    {
        // ---- audio window ------------------------------------------------------------------------------------------
        FakePipe pipe;
        uint64_t fc = 0;
        for (uint32_t s = 0; s < 120; s++) pipe.q.push_back(makePacket(s, fc));
        auto audio = std::make_shared<AudioCapture>(pipe);
        audio->reset();
        while (audio->pump() > 0) {}
        assert(audio->stats().packets == 120);

        bool started = false;
        AudioFrame* af = new AudioFrame(nullptr, {[&] { started = true; }, [] {}});
        af->OnAudioState(true, audio, "");
        af->Show();
        wxYield();

        wxBitmap bmp(640, 448);
        wxMemoryDC dc(bmp);
        af->Canvas()->SetWindowMs(20);
        af->Canvas()->SetGain(1);
        af->Canvas()->DrawTo(dc, wxSize(640, 448));
        dc.SelectObject(wxNullBitmap);
        wxImage img = bmp.ConvertToImage();
        int trace = 0, bg = 0;
        for (int y = 0; y < img.GetHeight(); y++)
            for (int x = 0; x < img.GetWidth(); x++) {
                if (img.GetRed(x, y) == 90 && img.GetGreen(x, y) == 209 && img.GetBlue(x, y) == 160) trace++;
                if (img.GetRed(x, y) == 16 && img.GetGreen(x, y) == 20 && img.GetBlue(x, y) == 24) bg++;
            }
        std::cout << "pixels de trace=" << trace << " fond=" << bg << "\n";
        assert(trace > 5000);          // 64 waveforms were drawn
        assert(bg > 20000);           // background kept (gain 1: the waveforms do not fill the cells)

        // wide layout draws too
        af->Canvas()->SetWide(true);
        wxBitmap bmp2(640, 448);
        wxMemoryDC dc2(bmp2);
        af->Canvas()->DrawTo(dc2, wxSize(640, 448));
        dc2.SelectObject(wxNullBitmap);

        // every layout and window of the lists draws (no crash, traces visible)
        for (const int cols : {2, 4, 8, 16, 32})
            for (const int ms : {2, 20, 2000}) {
                af->Canvas()->SetCols(cols);
                af->Canvas()->SetWindowMs(ms);
                wxBitmap b3(800, 448);
                wxMemoryDC d3(b3);
                af->Canvas()->DrawTo(d3, wxSize(800, 448));
                d3.SelectObject(wxNullBitmap);
                const wxImage i3 = b3.ConvertToImage();
                int t3 = 0;
                for (int y = 0; y < i3.GetHeight(); y += 2)
                    for (int x = 0; x < i3.GetWidth(); x += 2)
                        if (i3.GetRed(x, y) == 90 && i3.GetGreen(x, y) == 209 && i3.GetBlue(x, y) == 160) t3++;
                assert(t3 > 200);
            }
        const wxString st = AudioFrame::FormatStats(audio->stats(), audio->sampleRate());
        std::cout << st.ToStdString() << "\n";
        assert(st.Contains("packets 120") && st.Contains("lost 0 (0.0%)"));
        MicCounters mc; mc.dropped = 7; mc.level = 32768; mc.ringUsed = 21; mc.ringPeak = 838; mc.status = kMicStatusFull;
        const wxString hw = AudioFrame::FormatCounters(mc, audio->sampleRate());
        std::cout << hw.ToStdString() << "\n";
        assert(hw.Contains("dropped 7") && hw.Contains("FIFO 128 KB") && hw.Contains("was full") && hw.Contains("peak 838 pk"));
        af->SetCounters(mc);
        wxYield();
        af->Destroy();

        // ---- user I/O window ---------------------------------------------------------------------------------------------
        bool probed = false;
        UserIoFrame* uf = new UserIoFrame(nullptr, {[&] { probed = true; }, [] {}, [](uint32_t, bool) {}});
        uf->Show();
        wxYield();
        assert(probed && uf->GetMode() == UserIoFrame::Mode::Checking);
        uf->OnPresence(false, "");
        assert(uf->GetMode() == UserIoFrame::Mode::Absent);
        uf->OnPresence(true, "");
        assert(uf->GetMode() == UserIoFrame::Mode::Hardware);
        uf->OnState(UserIoDriver::LED_0, UserIoDriver::BUTTON_RESET);
        assert(uf->Leds() == UserIoDriver::LED_0 && uf->Buttons() == UserIoDriver::BUTTON_RESET);
        uf->Destroy();

        // simulated mode when the bitfile has no user I/O block
        UserIoFrame* sf = new UserIoFrame(nullptr, {[] {}, [] {}, [](uint32_t, bool) {}});
        sf->OnPresence(false, "");
        sf->SimulatePress(UserIoDriver::BUTTON_USER, true);             // ignored outside simulated mode
        assert(sf->Buttons() == 0);
        // switch to simulated mode through the real "Try the UI in simulated mode" button
        wxButton* simBtn = nullptr;
        std::deque<wxWindow*> todo{sf};
        while (!todo.empty()) {
            wxWindow* w = todo.front(); todo.pop_front();
            if (auto* b = dynamic_cast<wxButton*>(w)) if (b->GetLabel().Contains("simulated")) simBtn = b;
            for (wxWindow* c : w->GetChildren()) todo.push_back(c);
        }
        assert(simBtn);
        wxCommandEvent ev(wxEVT_BUTTON, simBtn->GetId());
        ev.SetEventObject(simBtn);
        simBtn->GetEventHandler()->ProcessEvent(ev);
        assert(sf->GetMode() == UserIoFrame::Mode::Simulated);
        sf->ToggleLed(UserIoDriver::LED_1);
        assert(sf->Leds() == UserIoDriver::LED_1);
        sf->SimulatePress(UserIoDriver::BUTTON_USER, true);
        assert(sf->Buttons() == UserIoDriver::BUTTON_USER);
        sf->SimulatePress(UserIoDriver::BUTTON_USER, false);
        assert(sf->Buttons() == 0);
        sf->Destroy();
        wxYield();
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
