/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// UserIoFrame.h — floating window for the board's two user LEDs and two push buttons (DARWIN / XEM8310): LED toggles and the
// live state of the buttons. Counterpart of Software/FP-Platform/src/UserIoPanel.tsx. The same UserIoDriver is what future
// application code (e.g. "a button starts a recording") would use.
//
// No dependency on the FrontPanel SDK. Hardware access happens on the camera worker thread, through the callbacks; with a
// bitfile that has no user I/O block the window says so, does not retry in the background, and offers a simulated mode.

#pragma once

#include <functional>
#include <memory>

#include <wx/wx.h>
#include <wx/tglbtn.h>

#include "UserIoDriver.h"

class UserIoFrame : public wxFrame {
public:
    struct Actions {
        std::function<void()> probe;                         // ask the worker: is the user I/O block present?
        std::function<void()> poll;                          // ask the worker to read LEDs + buttons
        std::function<void(uint32_t led, bool on)> setLed;   // ask the worker to drive a LED
    };
    enum class Mode { Checking, Hardware, Absent, Simulated };

    UserIoFrame(wxWindow* parent, Actions actions);

    // Worker -> GUI results (main thread)
    void OnPresence(bool present, const wxString& message);
    void OnState(uint32_t leds, uint32_t buttons);

    Mode GetMode() const { return m_mode; }
    uint32_t Leds() const { return m_leds; }
    uint32_t Buttons() const { return m_buttons; }
    // Test hooks: what a click on a LED button / a press of a button label does
    void ToggleLed(uint32_t led);
    void SimulatePress(uint32_t button, bool down);

private:
    void OnShow(wxShowEvent&);
    void OnCloseWindow(wxCloseEvent&);
    void OnTimer(wxTimerEvent&);
    void SetMode(Mode m);
    void UpdateView();

    Actions m_actions;
    Mode m_mode = Mode::Checking;
    bool m_probed = false;
    uint32_t m_leds = 0, m_buttons = 0;
    okcli::SimulatedUserIo m_sim;

    wxPanel* m_hw = nullptr;
    wxPanel* m_absent = nullptr;
    wxToggleButton* m_ledBtn[2] = {nullptr, nullptr};
    wxPanel* m_ledDot[2] = {nullptr, nullptr};
    wxPanel* m_btnDot[2] = {nullptr, nullptr};
    wxStaticText* m_title = nullptr;
    wxStaticText* m_note = nullptr;
    wxStaticText* m_msg = nullptr;
    wxTimer m_timer;
};
