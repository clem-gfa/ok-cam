/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// RgbViewport.cpp — see header. memcpy in UpdateImage, render in
// OnPaint.

#include "RgbViewport.h"

#include <wx/dcbuffer.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr std::size_t kMaxRgb = static_cast<std::size_t>(2304) * 1296 * 3;  // largest frame (AR0330)
}

RgbViewport::RgbViewport(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE) {
    m_buf.reserve(kMaxRgb);  // fixed capacity — UpdateImage never reallocates
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxBLACK);
    Bind(wxEVT_PAINT, &RgbViewport::OnPaint, this);
}

void RgbViewport::UpdateImage(const uint8_t* rgb, int width, int height) {
    if (!rgb || width <= 0 || height <= 0) return;
    const std::size_t n = static_cast<std::size_t>(width) * height * 3;
    if (n > kMaxRgb) return;
    m_buf.resize(n);                      // within reserved capacity → no reallocation
    std::memcpy(m_buf.data(), rgb, n);    // cheap copy out of the worker's lock-free buffer
    m_w = width;
    m_h = height;
    m_have = true;
    Refresh(false);                       // defer the actual render to OnPaint
}

void RgbViewport::ClearImage() {
    m_have = false;
    m_det = okcli::DetectionResult();
    Refresh(false);
}

void RgbViewport::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();
    if (!m_have || m_w <= 0 || m_h <= 0) return;

    const wxSize win = GetClientSize();
    if (win.x <= 0 || win.y <= 0) return;

    // Build a wxImage over our buffer (no copy: static data).
    wxImage img(m_w, m_h, m_buf.data(), /*static_data=*/true);
    double scale = 1.0;
    int offX = 0, offY = 0;
    if (m_scaleToFit) {
        const double s = std::min(static_cast<double>(win.x) / m_w, static_cast<double>(win.y) / m_h);
        const int w = std::max(1, static_cast<int>(m_w * s));
        const int h = std::max(1, static_cast<int>(m_h * s));
        wxBitmap bmp(img.Scale(w, h, wxIMAGE_QUALITY_NORMAL));
        offX = (win.x - w) / 2;
        offY = (win.y - h) / 2;
        scale = s;
        dc.DrawBitmap(bmp, offX, offY, false);
    } else {
        // 1:1 (Original): draw centered at native size (clipped by the panel if larger).
        wxBitmap bmp(img);
        offX = (win.x - m_w) / 2;
        offY = (win.y - m_h) / 2;
        dc.DrawBitmap(bmp, offX, offY, false);
    }
    if (!m_det.objects.empty() && m_det.imageW > 0 && m_det.imageH > 0) {
        // Detection boxes: image pixels of the analysed frame -> screen. One colour per class.
        const double kx = scale * m_w / m_det.imageW, ky = scale * m_h / m_det.imageH;
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        wxFont font = GetFont();
        font.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(font);
        for (const okcli::Detection& d : m_det.objects) {
            const int hue = (d.classId * 47) % 360;
            wxImage::HSVValue hsv(hue / 360.0, 0.85, 1.0);
            const wxImage::RGBValue rgb = wxImage::HSVtoRGB(hsv);
            const wxColour col(rgb.red, rgb.green, rgb.blue);
            dc.SetPen(wxPen(col, 2));
            const int x = offX + int(d.x * kx), y = offY + int(d.y * ky);
            dc.DrawRectangle(x, y, std::max(1, int(d.w * kx)), std::max(1, int(d.h * ky)));
            const wxString label = wxString::Format("%s %d%%", wxString::FromUTF8(d.label), int(d.score * 100 + 0.5f));
            const wxSize ts = dc.GetTextExtent(label);
            const int ly = std::max(0, y - ts.y - 2);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(col));
            dc.DrawRectangle(x, ly, ts.x + 6, ts.y + 2);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetTextForeground(*wxBLACK);
            dc.DrawText(label, x + 3, ly + 1);
        }
    }
}
