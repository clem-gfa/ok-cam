/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// OnnxYoloDetector.h — runs the YOLO26n model (Ultralytics ONNX export, NMS included) with ONNX Runtime.
// Only compiled into the GUI when ONNX Runtime was found at configure time (OKCAM_WITH_ONNX, see CMakeLists.txt);
// everything around the inference is in ObjectDetector.h.
//
// The model file: assets/models/yolo26n_320.onnx (staged next to the executable in models/), or any other YOLO26 / YOLOv10
// style end-to-end export ([1, N, 6] output: x1, y1, x2, y2, score, class). A model exported WITHOUT NMS ([1, 84, 8400])
// is refused with an explicit message, since it would need an NMS here.

#pragma once

#include <onnxruntime_cxx_api.h>

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ObjectDetector.h"

namespace okcli {

class OnnxYoloDetector : public IObjectDetector {
public:
    // `modelPath`: UTF-8 path of the .onnx file. Throws std::runtime_error (with a readable reason) if it cannot be used.
    explicit OnnxYoloDetector(const std::string& modelPath, int intraOpThreads = 0)
        : m_env(ORT_LOGGING_LEVEL_WARNING, "okcam") {
        try {
            const std::filesystem::path path = std::filesystem::u8path(modelPath);
            if (!std::filesystem::exists(path)) throw std::runtime_error("model file not found: " + modelPath);
            Ort::SessionOptions opts;
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            if (intraOpThreads > 0) opts.SetIntraOpNumThreads(intraOpThreads);
            m_session = std::make_unique<Ort::Session>(m_env, path.c_str(), opts);
            Ort::AllocatorWithDefaultOptions alloc;
            m_inputName = m_session->GetInputNameAllocated(0, alloc).get();
            m_outputName = m_session->GetOutputNameAllocated(0, alloc).get();
            const auto in = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            if (in.size() != 4 || in[1] != 3) throw std::runtime_error("unexpected model input (expected [1,3,H,W])");
            m_size = int(in[2] > 0 ? in[2] : 640);
            if (in[3] > 0 && in[3] != in[2]) throw std::runtime_error("only square model inputs are supported");
        } catch (const Ort::Exception& e) {
            throw std::runtime_error(std::string("ONNX Runtime: ") + e.what());
        }
    }

    int inputSize() const { return m_size; }

    std::vector<Detection> detect(const uint8_t* rgb, int w, int h, float threshold) override {
        const Letterbox lb = letterbox(rgb, w, h, m_size);
        try {
            const std::array<int64_t, 4> shape{1, 3, m_size, m_size};
            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input = Ort::Value::CreateTensor<float>(mem, const_cast<float*>(lb.chw.data()), lb.chw.size(),
                                                               shape.data(), shape.size());
            const char* inNames[] = {m_inputName.c_str()};
            const char* outNames[] = {m_outputName.c_str()};
            auto out = m_session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
            const auto info = out[0].GetTensorTypeAndShapeInfo();
            const auto dims = info.GetShape();
            if (dims.size() != 3 || dims[2] < 6 || dims[2] > 16)   // [1, N, 6]; a raw export is [1, 84, 8400]
                throw std::runtime_error("this model has no built-in NMS (output " + shapeText(dims) +
                                         "): export it with Ultralytics end-to-end / nms=True");
            return decodeDetections(out[0].GetTensorData<float>(), std::size_t(dims[1]), std::size_t(dims[2]), lb, w, h, threshold);
        } catch (const Ort::Exception& e) {
            throw std::runtime_error(std::string("ONNX Runtime: ") + e.what());
        }
    }

private:
    static std::string shapeText(const std::vector<int64_t>& d) {
        std::string s = "[";
        for (std::size_t i = 0; i < d.size(); i++) s += (i ? "," : "") + std::to_string(d[i]);
        return s + "]";
    }

    Ort::Env m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::string m_inputName, m_outputName;
    int m_size = 640;
};

}  // namespace okcli
