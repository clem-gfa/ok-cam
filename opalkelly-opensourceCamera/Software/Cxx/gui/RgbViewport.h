/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// RgbViewport.h — displays a packed-RGB frame, scaled to fit. A lightweight RGB viewport
// model: UpdateImage() does a cheap memcpy into a fixed buffer + Refresh(); the (heavier) image
// build + scale happens in OnPaint. No OpenGL/Bayer — the new ISP delivers already-debayered RGB.

#pragma once

#include <wx/wx.h>

#include <cstdint>
#include <vector>

class RgbViewport : public wxPanel {
public:
    explicit RgbViewport(wxWindow* parent);

    // Copy a packed-RGB frame (width*height*3) into our buffer and request a repaint.
    // GUI thread only. Cheap (memcpy + Refresh); rendering is deferred to OnPaint.
    void UpdateImage(const uint8_t* rgb, int width, int height);
    void ClearImage();

    // Image Size mode: true = scale-to-fit (default), false = 1:1 (Original, centered/clipped).
    void SetScaleToFit(bool on) { m_scaleToFit = on; Refresh(false); }

private:
    void OnPaint(wxPaintEvent&);

    std::vector<uint8_t> m_buf;  // fixed-capacity RGB buffer (GUI-thread-only)
    int m_w = 0, m_h = 0;
    bool m_have = false;
    bool m_scaleToFit = true;
};
