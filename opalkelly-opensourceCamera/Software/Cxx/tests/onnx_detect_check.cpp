// Verification manuelle (pas un test automatique) : OnnxYoloDetector sur une image RGB brute.
//   onnx_detect_check model.onnx image.rgb largeur hauteur [seuil]
// Affiche une ligne par objet ; a comparer avec le resultat d'Ultralytics sur la meme image.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "OnnxYoloDetector.h"

int main(int argc, char** argv) {
    if (argc < 5) { std::cerr << "usage: onnx_detect_check model.onnx image.rgb w h [threshold]\n"; return 2; }
    const int w = std::atoi(argv[3]), h = std::atoi(argv[4]);
    const float th = argc > 5 ? float(std::atof(argv[5])) : 0.25f;
    std::vector<uint8_t> img(std::size_t(w) * h * 3);
    std::ifstream f(argv[2], std::ios::binary);
    if (!f.read(reinterpret_cast<char*>(img.data()), std::streamsize(img.size()))) { std::cerr << "cannot read the image\n"; return 2; }
    try {
        okcli::OnnxYoloDetector det(argv[1]);
        std::printf("input %d x %d\n", det.inputSize(), det.inputSize());
        det.detect(img.data(), w, h, th);                         // warm-up
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<okcli::Detection> d;
        for (int i = 0; i < 20; i++) d = det.detect(img.data(), w, h, th);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
        for (const auto& o : d) std::printf("DET %s %.3f [%.1f, %.1f, %.1f, %.1f]\n", o.label.c_str(), o.score, o.x, o.y, o.x + o.w, o.y + o.h);
        std::printf("%.1f ms per detection (preprocessing included)\n", ms);
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
    return 0;
}
