#include "analysis/LsbDetection.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>

namespace analysis {

std::vector<LsbDetection> lsbDetection(const cv::Mat& img) {
    if (img.empty() || img.depth() != CV_8U || img.channels() == 2 || img.channels() > 4)
        throw std::invalid_argument("expected an 8-bit gray/BGR/BGRA image");

    const size_t cn     = static_cast<size_t>(img.channels());
    const size_t nColor = cn == 1 ? 1 : 3;
    const size_t rows   = static_cast<size_t>(img.rows);
    const size_t cols   = static_cast<size_t>(img.cols);
    const size_t stride = static_cast<size_t>(img.step[0]);
    const size_t extent = (rows - 1) * stride + (cols - 1) * cn + 1;  // from the channel's first sample

    std::vector<LsbDetection> out;
    for (size_t c = 0; c < nColor; ++c) {
        const Plane8 plane{std::span<const uint8_t>(img.data + c, extent), cols, rows, cn, stride};
        out.push_back({rsAnalysis(plane), samplePairs(plane)});
    }
    return out;
}

}  // namespace analysis
