/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// ObjectDetector.h — object detection on the camera image (C++ counterpart of Software/FP-Platform/src/YoloDetector.ts).
//
// Same model family and same pre/post-processing as the TypeScript app: a YOLO26n network trained on COCO (80 classes),
// exported by Ultralytics WITH its NMS, so the output is [1, N, 6] = x1, y1, x2, y2, score, class (pixels of the
// letterboxed model input). Only the inference itself needs a runtime (ONNX Runtime, see OnnxYoloDetector.h); everything
// else is here, header-only and free of any library, so that it is unit-tested without model or runtime:
//
//   letterbox()         RGB frame -> float CHW tensor in [0,1], aspect ratio kept, grey (114) borders
//   decodeDetections()  model output -> boxes in image pixels
//   DetectionWorker     runs a detector on its own thread; the GUI hands it frames (never blocking, the newest frame
//                       wins) and reads the latest result, so detection never slows the acquisition or the display.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace okcli {

struct Detection {
    int         classId = 0;
    std::string label;
    float       score = 0;
    float       x = 0, y = 0, w = 0, h = 0;   // box in pixels of the analysed image (top-left corner + size)
};

struct DetectionResult {
    std::vector<Detection> objects;
    int    imageW = 0, imageH = 0;            // size of the image the boxes refer to
    double inferenceMs = 0;                   // preprocessing + inference + decoding
    uint64_t sequence = 0;                    // increments with every new result
};

inline const char* cocoClassName(int id) {
    static const char* const names[80] = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
        "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
        "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
        "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
        "hair drier", "toothbrush"};
    return (id >= 0 && id < 80) ? names[id] : "unknown";
}

// ---- pre-processing -------------------------------------------------------------------------------------------------

struct Letterbox {
    std::vector<float> chw;      // 3 * size * size floats (R plane, G plane, B plane), values in [0,1]
    float scale = 1;             // model pixels per image pixel
    float padX = 0, padY = 0;    // left / top border in model pixels
    int   size = 0;              // the model input is size x size
};

// Resizes `rgb` (packed 8-bit RGB, w x h) to fit a size x size square (bilinear, aspect ratio kept), centres it on a
// grey (114/255) background and converts it to planar float, as Ultralytics does.
inline Letterbox letterbox(const uint8_t* rgb, int w, int h, int size) {
    Letterbox lb;
    lb.size = size;
    const std::size_t plane = std::size_t(size) * size;
    lb.chw.assign(3 * plane, 114.0f / 255.0f);
    if (!rgb || w <= 0 || h <= 0 || size <= 0) return lb;
    lb.scale = std::min(float(size) / float(w), float(size) / float(h));
    const int newW = std::max(1, int(std::lround(w * lb.scale)));
    const int newH = std::max(1, int(std::lround(h * lb.scale)));
    const int left = (size - newW) / 2, top = (size - newH) / 2;
    lb.padX = float(left);
    lb.padY = float(top);
    const float sx = float(w) / float(newW), sy = float(h) / float(newH);
    for (int y = 0; y < newH; y++) {
        const float fy = std::max(0.0f, (float(y) + 0.5f) * sy - 0.5f);
        const int y0 = std::min(int(fy), h - 1), y1 = std::min(y0 + 1, h - 1);
        const float wy = fy - float(y0);
        for (int x = 0; x < newW; x++) {
            const float fx = std::max(0.0f, (float(x) + 0.5f) * sx - 0.5f);
            const int x0 = std::min(int(fx), w - 1), x1 = std::min(x0 + 1, w - 1);
            const float wx = fx - float(x0);
            const std::size_t o = std::size_t(top + y) * size + std::size_t(left + x);
            for (int c = 0; c < 3; c++) {
                const float a = rgb[(std::size_t(y0) * w + x0) * 3 + c], b = rgb[(std::size_t(y0) * w + x1) * 3 + c];
                const float d = rgb[(std::size_t(y1) * w + x0) * 3 + c], e = rgb[(std::size_t(y1) * w + x1) * 3 + c];
                const float v = (a * (1 - wx) + b * wx) * (1 - wy) + (d * (1 - wx) + e * wx) * wy;
                lb.chw[std::size_t(c) * plane + o] = v / 255.0f;
            }
        }
    }
    return lb;
}

// ---- post-processing ------------------------------------------------------------------------------------------------

// `data`: rows x cols floats (cols >= 6): x1, y1, x2, y2, score, class. Keeps score >= threshold, undoes the letterbox and
// clamps to the image. Rows are assumed sorted by the model (NMS already applied).
inline std::vector<Detection> decodeDetections(const float* data, std::size_t rows, std::size_t cols, const Letterbox& lb,
                                               int imageW, int imageH, float threshold) {
    std::vector<Detection> out;
    if (!data || cols < 6 || lb.scale <= 0) return out;
    auto clampTo = [](float v, float hi) { return std::max(0.0f, std::min(v, hi)); };
    for (std::size_t i = 0; i < rows; i++) {
        const float* r = data + i * cols;
        const float score = r[4];
        if (!(score >= threshold)) continue;      // also drops NaN
        const float x1 = clampTo((r[0] - lb.padX) / lb.scale, float(imageW));
        const float y1 = clampTo((r[1] - lb.padY) / lb.scale, float(imageH));
        const float x2 = clampTo((r[2] - lb.padX) / lb.scale, float(imageW));
        const float y2 = clampTo((r[3] - lb.padY) / lb.scale, float(imageH));
        if (x2 <= x1 || y2 <= y1) continue;
        Detection d;
        d.classId = int(std::lround(r[5]));
        d.label = cocoClassName(d.classId);
        d.score = score;
        d.x = x1; d.y = y1; d.w = x2 - x1; d.h = y2 - y1;
        out.push_back(std::move(d));
    }
    return out;
}

// ---- detector interface ---------------------------------------------------------------------------------------------

class IObjectDetector {
public:
    virtual ~IObjectDetector() = default;
    // Detects objects in a packed RGB image. Called from the detection thread only.
    virtual std::vector<Detection> detect(const uint8_t* rgb, int w, int h, float threshold) = 0;
};

// ---- background worker ----------------------------------------------------------------------------------------------

class DetectionWorker {
public:
    explicit DetectionWorker(std::unique_ptr<IObjectDetector> detector) : m_detector(std::move(detector)) {
        m_thread = std::thread([this] { run(); });
    }
    ~DetectionWorker() {
        {
            std::lock_guard<std::mutex> l(m_mx);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    DetectionWorker(const DetectionWorker&) = delete;
    DetectionWorker& operator=(const DetectionWorker&) = delete;

    void setThreshold(float t) { m_threshold.store(t); }

    // Offers a frame. Returns false (and copies nothing) while the previous one is still being analysed: the caller just
    // tries again with a later frame. Cheap: one memcpy of the image.
    bool submit(const uint8_t* rgb, int w, int h) {
        if (!rgb || w <= 0 || h <= 0) return false;
        std::lock_guard<std::mutex> l(m_mx);
        if (m_pending || m_busy) return false;
        m_image.assign(rgb, rgb + std::size_t(w) * h * 3);
        m_w = w;
        m_h = h;
        m_pending = true;
        m_cv.notify_one();
        return true;
    }

    // The most recent result (empty before the first one). Thread-safe.
    DetectionResult latest() const {
        std::lock_guard<std::mutex> l(m_mx);
        return m_result;
    }
    // Why the detector failed (model missing, runtime error...), empty if all is well.
    std::string error() const {
        std::lock_guard<std::mutex> l(m_mx);
        return m_error;
    }

private:
    void run() {
        for (;;) {
            std::vector<uint8_t> image;
            int w = 0, h = 0;
            {
                std::unique_lock<std::mutex> l(m_mx);
                m_cv.wait(l, [this] { return m_stop || m_pending; });
                if (m_stop) return;
                image.swap(m_image);
                w = m_w;
                h = m_h;
                m_pending = false;
                m_busy = true;
            }
            DetectionResult res;
            std::string err;
            const auto t0 = std::chrono::steady_clock::now();
            try {
                res.objects = m_detector->detect(image.data(), w, h, m_threshold.load());
            } catch (const std::exception& e) {
                err = e.what();
            }
            res.imageW = w;
            res.imageH = h;
            res.inferenceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            {
                std::lock_guard<std::mutex> l(m_mx);
                if (err.empty()) {
                    res.sequence = m_result.sequence + 1;
                    m_result = std::move(res);
                    m_error.clear();
                } else {
                    m_error = err;
                }
                m_busy = false;
                m_image.swap(image);     // keep the allocation for the next frame
            }
        }
    }

    std::unique_ptr<IObjectDetector> m_detector;
    std::atomic<float> m_threshold{0.25f};
    mutable std::mutex m_mx;
    std::condition_variable m_cv;
    std::vector<uint8_t> m_image;
    int m_w = 0, m_h = 0;
    bool m_pending = false, m_busy = false, m_stop = false;
    DetectionResult m_result;
    std::string m_error;
    std::thread m_thread;
};

}  // namespace okcli
