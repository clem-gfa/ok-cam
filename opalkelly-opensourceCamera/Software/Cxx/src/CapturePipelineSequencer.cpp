/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// CapturePipelineSequencer.cpp — see header.

#include "CapturePipelineSequencer.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace okcli {
namespace {
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

CapturePipelineSequencer::CapturePipelineSequencer(IAxiLite& axiLite, IAxiStream& axiStream,
                                                   CameraMode cameraMode, IISP& isp, ITPG& tpg,
                                                   ICameraControl& cameraControl)
    : m_axiLite(axiLite),
      m_axiStream(axiStream),
      m_cameraMode(cameraMode),
      m_isp(isp),
      m_tpg(tpg),
      m_cameraControl(cameraControl),
      m_vdma(axiLite),
      m_histogram(axiLite),
      m_streamSwitch(axiLite) {}

void CapturePipelineSequencer::assertPipelineResets() {
    // Stop the pipeline without asserting system reset (I2C state remains valid).
    if (m_pipelineRunning) {
        if (!m_vdma.stopWriteChannel()) std::fprintf(stderr, "assertPipelineResets: S2MM did not halt\n");
        if (!m_vdma.stopReadChannel())  std::fprintf(stderr, "assertPipelineResets: MM2S did not halt\n");
        m_tpg.stop();
        m_isp.stop();
        m_histogram.stop();
        m_vdma.softReset();
        m_pipelineRunning = false;
    }
}

void CapturePipelineSequencer::stopPipeline() {
    if (!m_vdma.stopWriteChannel()) std::fprintf(stderr, "stopPipeline: S2MM did not halt, continuing\n");
    if (!m_vdma.stopReadChannel())  std::fprintf(stderr, "stopPipeline: MM2S did not halt, continuing\n");

    m_tpg.stop();
    m_isp.stop();
    m_histogram.stop();

    m_axiLite.resetSystem();
    m_vdma.softReset();

    // Let IPs exit reset cleanly. resetSystem() asserts axis_aresetn (resets stream-domain IPs).
    // In szgcam vid_clk comes from the sensor LVDS clock; in pcam (also used for TPG mode) it
    // comes from clk_wiz and is always available.
    sleepMs(100);
    m_pipelineRunning = false;
}

bool CapturePipelineSequencer::captureFrame(CapturedFrame& out) {
    if (!m_pipelineRunning) {
        std::fprintf(stderr, "captureFrame: skipped, pipeline not running\n");
        return false;
    }

    const int width = m_width;
    const int height = m_height;
    const std::size_t numPixels = static_cast<std::size_t>(width) * height;

    // Route video (SI0) and pull the frame straight off the wire.
    m_streamSwitch.setSlave(0);
    out.image.assign(numPixels * BYTES_PER_PIXEL, 0);
    m_axiStream.read(out.image.data(), out.image.size(), STREAM_TIMEOUT_MS);

    // Fast change-detection hash: sample every 1024th byte.
    int32_t hash = 0;
    for (std::size_t i = 0; i < out.image.size(); i += 1024) {
        hash = static_cast<int32_t>(hash * 31 + out.image[i]);
    }
    out.frameChanged = (hash != m_prevFrameHash);
    m_prevFrameHash = hash;

    // The gateware will not emit the histogram until the image is drained, nor the next image
    // until the histogram is drained.
    out.histogram.assign(HISTOGRAM_SAMPLES, 0);
    m_streamSwitch.setSlave(1);
    m_axiStream.read(reinterpret_cast<uint8_t*>(out.histogram.data()),
                     out.histogram.size() * sizeof(uint32_t), STREAM_TIMEOUT_MS);

    out.width = width;
    out.height = height;
    return true;
}

bool CapturePipelineSequencer::captureOnce(CapturedFrame& out) {
    CapturedFrame stale;
    if (!captureFrame(stale)) return false;  // discard stale triple-buffer frame
    return captureFrame(out);                // return fresh
}

void CapturePipelineSequencer::reconfigurePipeline() {
    const int width = m_width;
    const int height = m_height;
    const int horizontalSizeBytes = width * BYTES_PER_PIXEL;
    const uint32_t frameSize = static_cast<uint32_t>(horizontalSizeBytes) * height;

    const uint32_t buf0 = DDR_BASE_ADDR;
    const uint32_t buf1 = buf0 + frameSize;
    const uint32_t buf2 = buf1 + frameSize;

    try {
        stopPipeline();
        m_cameraControl.reinitializeI2C();
        configureIPs(width, height);
        startupPipeline(horizontalSizeBytes, height, buf0, buf1, buf2, width);
        m_pipelineRunning = true;
        m_cameraControl.setExposure(m_cameraControl.exposure());
    } catch (...) {
        m_pipelineRunning = false;
        throw;
    }
}

void CapturePipelineSequencer::configureIPs(int width, int height) {
    m_tpg.setResolution(width, height);
    m_tpg.setPattern(m_tpg.patternId());
    m_tpg.setMotionSpeed(m_tpg.motionSpeed());

    m_isp.initialize(width, height, m_isp.awb(), m_isp.rgain(), m_isp.ggain(), m_isp.bgain());

    m_histogram.initialize(height, width);

    // NOTE: the stream switch is intentionally NOT configured here. After reset, ROUTING_MODE=1
    // disables all routes, keeping the pipeline stalled until flushFrame() configures the switch
    // and reads (pre-configuring risks corrupting VDMA genlock).
}

void CapturePipelineSequencer::startupPipeline(int horizontalSizeBytes, int height, uint32_t buf0,
                                               uint32_t buf1, uint32_t buf2, int width) {
    // Order matters: S2MM first, then IPs, then MM2S.
    m_vdma.startWriteChannel(horizontalSizeBytes, height, buf0, buf1, buf2);
    m_tpg.start(m_cameraMode != CameraMode::Tpg);
    m_isp.start();
    m_histogram.start();
    m_vdma.startReadChannel(horizontalSizeBytes, height, buf0, buf1, buf2);

    // Wait for the expected VDMAIntErr (SR bit4), then clear status.
    for (int i = 0; i < 10; ++i) {
        sleepMs(50);
        if (m_vdma.getWriteChannelStatus() & (1u << 4)) break;  // VDMAIntErr (expected)
    }
    m_vdma.clearStatus();

    // Flush the first frame (buffer 0 is never written in triple-buffer mode).
    flushFrame(width, height);
}

void CapturePipelineSequencer::flushFrame(int width, int height) {
    const std::size_t frameSize = static_cast<std::size_t>(width) * height * BYTES_PER_PIXEL;
    std::vector<uint8_t> frameBuffer(frameSize);
    m_streamSwitch.setSlave(0);
    m_axiStream.read(frameBuffer.data(), frameBuffer.size(), STREAM_TIMEOUT_MS);

    std::vector<uint32_t> histogramBuffer(HISTOGRAM_SAMPLES);
    m_streamSwitch.setSlave(1);
    m_axiStream.read(reinterpret_cast<uint8_t*>(histogramBuffer.data()),
                     histogramBuffer.size() * sizeof(uint32_t), STREAM_TIMEOUT_MS);
}

}  // namespace okcli
