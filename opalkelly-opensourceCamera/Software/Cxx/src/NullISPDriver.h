/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// NullISPDriver.h — stub ISP for the no-camera bitfiles.
//
// The nocam bitfiles omit the HLS ISP IP entirely (video comes from the on-chip TPG), so there
// is nothing to process. All methods are no-ops; gains/AWB are held in memory only so UI reads
// through IISP stay consistent.

#pragma once

#include <cstdint>

#include "IISP.h"

namespace okcli {

class NullISPDriver : public IISP {
public:
    uint32_t rgain() const override { return m_rgain; }
    uint32_t ggain() const override { return m_ggain; }
    uint32_t bgain() const override { return m_bgain; }
    uint32_t awb() const override { return m_awb; }

    void setGains(uint32_t rgain, uint32_t ggain, uint32_t bgain) override {
        m_rgain = rgain;
        m_ggain = ggain;
        m_bgain = bgain;
    }

    void setAWBThreshold(uint32_t awb) override { m_awb = awb; }

    void initialize(uint32_t /*width*/, uint32_t /*height*/, uint32_t awbThresh, uint32_t rgain,
                    uint32_t ggain, uint32_t bgain) override {
        m_awb = awbThresh;
        m_rgain = rgain;
        m_ggain = ggain;
        m_bgain = bgain;
    }

    void start() override {}
    void stop() override {}

private:
    uint32_t m_rgain = 128, m_ggain = 128, m_bgain = 128, m_awb = 255;
};

}  // namespace okcli
