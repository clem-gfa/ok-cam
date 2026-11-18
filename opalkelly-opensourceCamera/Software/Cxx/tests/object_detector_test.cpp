// Test hors materiel de ObjectDetector.h : letterbox, decodage des detections, DetectionWorker (detecteur factice).
// Build : g++ -std=c++17 -I../src object_detector_test.cpp -pthread -o object_detector_test
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "ObjectDetector.h"

using namespace okcli;

static bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

int main() {
    // ---- letterbox: 200 x 100 image into 100 x 100: scale 0.5, 50 x 50 image, 25 rows of grey above and below
    {
        std::vector<uint8_t> img(200 * 100 * 3);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 200; x++) { img[(y * 200 + x) * 3] = 255; img[(y * 200 + x) * 3 + 1] = 0; img[(y * 200 + x) * 3 + 2] = 128; }
        const Letterbox lb = letterbox(img.data(), 200, 100, 100);
        assert(lb.size == 100 && lb.chw.size() == 3u * 100 * 100);
        assert(near(lb.scale, 0.5f) && near(lb.padX, 0) && near(lb.padY, 25));
        const std::size_t plane = 100 * 100;
        assert(near(lb.chw[0], 114.f / 255));                         // border (R plane)
        assert(near(lb.chw[50 * 100 + 50], 1.0f));                    // inside: R = 255
        assert(near(lb.chw[plane + 50 * 100 + 50], 0.0f));            // G = 0
        assert(near(lb.chw[2 * plane + 50 * 100 + 50], 128.f / 255)); // B = 128
        assert(near(lb.chw[24 * 100 + 50], 114.f / 255) && near(lb.chw[75 * 100 + 50], 114.f / 255));   // rows 24 and 75: borders
        assert(near(lb.chw[25 * 100 + 50], 1.0f) && near(lb.chw[74 * 100 + 50], 1.0f));                // rows 25 and 74: image
    }
    // ---- letterbox of a tall image: pad left / right
    {
        std::vector<uint8_t> img(50 * 100 * 3, 200);
        const Letterbox lb = letterbox(img.data(), 50, 100, 100);
        assert(near(lb.scale, 1.0f) && near(lb.padX, 25) && near(lb.padY, 0));
    }
    // ---- decode: undo the letterbox, threshold, clamp, drop empty boxes
    {
        Letterbox lb; lb.size = 100; lb.scale = 0.5f; lb.padX = 0; lb.padY = 25;
        const float rows[4 * 6] = {
            10, 35, 60, 65, 0.90f, 0,      // person: x 20..120, y 20..80 in the image
            10, 35, 60, 65, 0.10f, 2,      // below the threshold
            -20, 0, 120, 100, 0.80f, 56,   // chair, clamped to the 200 x 100 image
            40, 40, 40, 60, 0.95f, 1,      // zero width
        };
        const auto d = decodeDetections(rows, 4, 6, lb, 200, 100, 0.25f);
        assert(d.size() == 2);
        assert(d[0].label == "person" && near(d[0].x, 20) && near(d[0].y, 20) && near(d[0].w, 100) && near(d[0].h, 60) && near(d[0].score, 0.9f));
        assert(d[1].label == "chair" && near(d[1].x, 0) && near(d[1].y, 0) && near(d[1].w, 200) && near(d[1].h, 100));
        assert(decodeDetections(nullptr, 4, 6, lb, 200, 100, 0.25f).empty());
        assert(decodeDetections(rows, 4, 5, lb, 200, 100, 0.25f).empty());
        assert(std::string(cocoClassName(79)) == "toothbrush" && std::string(cocoClassName(80)) == "unknown" && std::string(cocoClassName(-1)) == "unknown");
    }
    // ---- worker: runs on its own thread, drops frames while busy, keeps the latest result, reports errors
    {
        struct Fake : IObjectDetector {
            std::atomic<int>* calls; std::atomic<bool>* slow; std::atomic<bool>* fail;
            std::vector<Detection> detect(const uint8_t* rgb, int w, int h, float th) override {
                (*calls)++;
                if (*slow) std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if (*fail) throw std::runtime_error("model exploded");
                Detection d; d.label = "x"; d.score = th; d.w = float(w); d.h = float(h); d.x = rgb[0];
                return {d};
            }
        };
        std::atomic<int> calls{0}; std::atomic<bool> slow{true}, fail{false};
        auto fake = std::make_unique<Fake>(); fake->calls = &calls; fake->slow = &slow; fake->fail = &fail;
        DetectionWorker w(std::move(fake));
        w.setThreshold(0.5f);
        const std::vector<uint8_t> img(8 * 4 * 3, 7);
        assert(w.latest().sequence == 0);
        assert(w.submit(img.data(), 8, 4));
        assert(!w.submit(img.data(), 8, 4));          // busy: refused, not queued
        assert(!w.submit(nullptr, 8, 4) && !w.submit(img.data(), 0, 4));
        for (int i = 0; i < 100 && w.latest().sequence == 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const DetectionResult r = w.latest();
        assert(r.sequence == 1 && calls == 1 && r.imageW == 8 && r.imageH == 4 && r.objects.size() == 1);
        assert(near(r.objects[0].score, 0.5f) && near(r.objects[0].x, 7) && r.inferenceMs >= 100);
        slow = false; fail = true;
        for (int i = 0; i < 100 && w.error().empty(); i++) { w.submit(img.data(), 8, 4); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        assert(w.error() == "model exploded" && w.latest().sequence == 1);   // the previous result is kept
        fail = false;
        for (int i = 0; i < 100 && w.latest().sequence < 2; i++) { w.submit(img.data(), 8, 4); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        assert(w.latest().sequence >= 2 && w.error().empty());
    }
    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
