/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UserIoFrame.h"

using okcli::UserIoDriver;

namespace {
wxPanel* MakeDot(wxWindow* parent) {
    auto* p = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(14, 14));
    p->SetBackgroundColour(wxColour(51, 51, 51));
    return p;
}
void SetDot(wxPanel* p, bool on, const wxColour& colour) {
    p->SetBackgroundColour(on ? colour : wxColour(51, 51, 51));
    p->Refresh();
}
}  // namespace

UserIoFrame::UserIoFrame(wxWindow* parent, Actions actions)
    : wxFrame(parent, wxID_ANY, "Board user I/O", wxDefaultPosition, wxDefaultSize,
              wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU | wxMINIMIZE_BOX | wxFRAME_FLOAT_ON_PARENT),
      m_actions(std::move(actions)), m_timer(this) {
    auto* root = new wxPanel(this);
    auto* col = new wxBoxSizer(wxVERTICAL);

    m_title = new wxStaticText(root, wxID_ANY, "Checking the bitfile...");
    col->Add(m_title, 0, wxALL, 10);

    // hardware / simulated controls
    m_hw = new wxPanel(root);
    auto* hw = new wxBoxSizer(wxVERTICAL);
    auto* leds = new wxBoxSizer(wxHORIZONTAL);
    for (int i = 0; i < 2; i++) {
        m_ledDot[i] = MakeDot(m_hw);
        m_ledBtn[i] = new wxToggleButton(m_hw, wxID_ANY, i == 0 ? "LED_0" : "LED_1");
        const uint32_t bit = i == 0 ? UserIoDriver::LED_0 : UserIoDriver::LED_1;
        m_ledBtn[i]->Bind(wxEVT_TOGGLEBUTTON, [this, bit](wxCommandEvent&) { ToggleLed(bit); });
        leds->Add(m_ledDot[i], 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
        leds->Add(m_ledBtn[i], 0, wxRIGHT, 14);
    }
    hw->Add(leds, 0, wxBOTTOM, 10);
    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    for (int i = 0; i < 2; i++) {
        m_btnDot[i] = MakeDot(m_hw);
        auto* label = new wxStaticText(m_hw, wxID_ANY, i == 0 ? "User button" : "Reset button");
        const uint32_t bit = i == 0 ? UserIoDriver::BUTTON_USER : UserIoDriver::BUTTON_RESET;
        // in simulated mode, holding the mouse on the label simulates a press
        for (wxWindow* w : std::initializer_list<wxWindow*>{label, m_btnDot[i]}) {
            w->Bind(wxEVT_LEFT_DOWN, [this, bit](wxMouseEvent&) { SimulatePress(bit, true); });
            w->Bind(wxEVT_LEFT_UP, [this, bit](wxMouseEvent&) { SimulatePress(bit, false); });
            w->Bind(wxEVT_LEAVE_WINDOW, [this, bit](wxMouseEvent&) { SimulatePress(bit, false); });
        }
        btns->Add(m_btnDot[i], 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 4);
        btns->Add(label, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 14);
    }
    hw->Add(btns, 0);
    m_note = new wxStaticText(m_hw, wxID_ANY, "");
    hw->Add(m_note, 0, wxTOP, 8);
    m_hw->SetSizer(hw);
    col->Add(m_hw, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // bitfile without the block
    m_absent = new wxPanel(root);
    auto* ab = new wxBoxSizer(wxHORIZONTAL);
    auto* sim = new wxButton(m_absent, wxID_ANY, "Try the UI in simulated mode");
    auto* retry = new wxButton(m_absent, wxID_ANY, "Retry");
    sim->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetMode(Mode::Simulated); });
    retry->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetMode(Mode::Checking); if (m_actions.probe) m_actions.probe(); });
    ab->Add(sim, 0, wxRIGHT, 6);
    ab->Add(retry, 0);
    m_absent->SetSizer(ab);
    col->Add(m_absent, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    m_msg = new wxStaticText(root, wxID_ANY, "");
    m_msg->SetForegroundColour(wxColour(200, 120, 20));
    col->Add(m_msg, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    root->SetSizer(col);
    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(root, 1, wxEXPAND);
    SetSizerAndFit(top);

    Bind(wxEVT_TIMER, &UserIoFrame::OnTimer, this);
    Bind(wxEVT_SHOW, &UserIoFrame::OnShow, this);
    Bind(wxEVT_CLOSE_WINDOW, &UserIoFrame::OnCloseWindow, this);
    SetMode(Mode::Checking);
}

void UserIoFrame::OnCloseWindow(wxCloseEvent& e) {
    if (e.CanVeto()) { e.Veto(); Hide(); }
    else Destroy();
}

void UserIoFrame::OnShow(wxShowEvent& e) {
    if (e.IsShown()) {
        if (!m_probed) {                  // one presence check per session, never repeated automatically
            m_probed = true;
            if (m_actions.probe) m_actions.probe();
        }
        m_timer.Start(250);               // 4 Hz
    } else {
        m_timer.Stop();
    }
    e.Skip();
}

void UserIoFrame::OnTimer(wxTimerEvent&) {
    if (m_mode == Mode::Hardware) {
        if (m_actions.poll) m_actions.poll();
    } else if (m_mode == Mode::Simulated) {
        OnState(m_sim.leds(), m_sim.buttonsPressed());
    }
}

void UserIoFrame::SetMode(Mode m) {
    m_mode = m;
    switch (m) {
        case Mode::Checking:  m_title->SetLabel("Checking the bitfile..."); break;
        case Mode::Hardware:  m_title->SetLabel("Board user I/O"); break;
        case Mode::Absent:
            m_title->SetLabel("The loaded bitfile has no user I/O block\n(build the gateware with 'set with_userio 1').");
            break;
        case Mode::Simulated: m_title->SetLabel("Board user I/O - simulated, no hardware access"); break;
    }
    m_hw->Show(m == Mode::Hardware || m == Mode::Simulated);
    m_absent->Show(m == Mode::Absent);
    m_note->SetLabel(m == Mode::Simulated ? "Hold a button label with the mouse to simulate a press." : "");
    GetSizer()->SetSizeHints(this);
    Layout();
    Fit();
}

void UserIoFrame::OnPresence(bool present, const wxString& message) {
    m_msg->SetLabel(message);
    SetMode(present ? Mode::Hardware : Mode::Absent);
}

void UserIoFrame::OnState(uint32_t leds, uint32_t buttons) {
    m_leds = leds;
    m_buttons = buttons;
    UpdateView();
}

void UserIoFrame::UpdateView() {
    const wxColour red(255, 77, 77), green(90, 209, 160);
    for (int i = 0; i < 2; i++) {
        const uint32_t led = i == 0 ? UserIoDriver::LED_0 : UserIoDriver::LED_1;
        const uint32_t btn = i == 0 ? UserIoDriver::BUTTON_USER : UserIoDriver::BUTTON_RESET;
        SetDot(m_ledDot[i], (m_leds & led) != 0, red);
        m_ledBtn[i]->SetValue((m_leds & led) != 0);
        SetDot(m_btnDot[i], (m_buttons & btn) != 0, green);
    }
}

void UserIoFrame::ToggleLed(uint32_t led) {
    const bool on = (m_leds & led) == 0;
    if (m_mode == Mode::Simulated) m_sim.setLed(led, on);
    else if (m_mode == Mode::Hardware && m_actions.setLed) m_actions.setLed(led, on);
    else return;
    m_leds = on ? (m_leds | led) : (m_leds & ~led);
    UpdateView();
}

void UserIoFrame::SimulatePress(uint32_t button, bool down) {
    if (m_mode != Mode::Simulated) return;
    m_sim.press(button, down);
    OnState(m_sim.leds(), m_sim.buttonsPressed());
}
