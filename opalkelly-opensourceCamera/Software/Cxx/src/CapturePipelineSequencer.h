/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// CapturePipelineSequencer.h — capture pipeline orchestrator.
//
// Sequences the AMD/Xilinx capture pipeline: enforces IP start/stop ordering (VDMA, ISP, TPG,
// histogram, stream switch), manages reset timing, and coordinates frame/histogram data flow.
// Works for both the native-AXI (SZG-HUB1450) and Classic-over-bridge (XEM8320) paths via the
// IAxiLite / IAxiStream abstractions.
//
// This implementation is single-threaded and synchronous: there is no async work queue — calls
// run in order on the caller's thread.

#pragma once

#include <cstdint>
#include <vector>

#include "Axi.h"
#include "CameraTypes.h"
#include "HistogramDriver.h"
#include "ICameraControl.h"
#include "IISP.h"
#include "ITPG.h"
#include "StreamSwitchDriver.h"
#include "VideoDMADriver.h"

namespace okcli {

// One captured frame: GBR-packed image (width*height*3 bytes, byte0=G, byte1=B, byte2=R) plus a
// 768-sample histogram (256 bins × 3 channels).
struct CapturedFrame {
    std::vector<uint8_t> image;
    int width = 0;
    int height = 0;
    std::vector<uint32_t> histogram;
    bool frameChanged = false;
};

class CapturePipelineSequencer {
public:
    CapturePipelineSequencer(IAxiLite& axiLite, IAxiStream& axiStream, CameraMode cameraMode,
                             IISP& isp, ITPG& tpg, ICameraControl& cameraControl);

    MatrixDimensions frameDimensions() const { return m_frameDimensions; }

    // Pipeline configuration.
    void setResolution(int width, int height) { m_width = width; m_height = height; }
    void setFrameDimensions(const MatrixDimensions& dims) { m_frameDimensions = dims; }
    void initializePipeline() {}  // streaming pipeline needs no init

    // Lifecycle.
    void logicReset() { reconfigurePipeline(); }  // full stop+reconfigure+start
    void assertPipelineResets();                  // soft stop (I2C stays valid)
    void stopPipeline();                          // stop + system reset (invalidates I2C)

    bool pipelineRunning() const { return m_pipelineRunning; }

    // Capture one frame (the capture-loop body). Returns false if the pipeline is not running.
    bool captureFrame(CapturedFrame& out);

    // Capture a single frame on demand: discard the stale triple-buffer frame, return the fresh one.
    bool captureOnce(CapturedFrame& out);

private:
    void reconfigurePipeline();
    void configureIPs(int width, int height);
    void startupPipeline(int horizontalSizeBytes, int height, uint32_t buf0, uint32_t buf1,
                         uint32_t buf2, int width);
    void flushFrame(int width, int height);

    static constexpr int      BYTES_PER_PIXEL   = 3;
    static constexpr uint32_t DDR_BASE_ADDR     = 0x80000000u;
    static constexpr uint32_t STREAM_TIMEOUT_MS = 5000;
    static constexpr int      HISTOGRAM_SAMPLES = 256 * 3;  // 768 u32

    IAxiLite& m_axiLite;
    IAxiStream& m_axiStream;
    CameraMode m_cameraMode;
    IISP& m_isp;
    ITPG& m_tpg;
    ICameraControl& m_cameraControl;

    VideoDMADriver m_vdma;
    HistogramDriver m_histogram;
    StreamSwitchDriver m_streamSwitch;

    MatrixDimensions m_frameDimensions{0, 0};
    int m_width = 0;
    int m_height = 0;
    bool m_pipelineRunning = false;
    int32_t m_prevFrameHash = 0;
};

}  // namespace okcli
