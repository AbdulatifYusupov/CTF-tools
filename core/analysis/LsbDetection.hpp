#pragma once
#include "analysis/Steganalysis.hpp"

#include <opencv2/core.hpp>

#include <vector>

namespace analysis {

struct LsbDetection {
    LsbEstimate rs, spa;
};

// RS and sample-pairs estimates per colour channel: one entry for gray, three (B, G, R) for
// BGR/BGRA (alpha is ignored). 8-bit images only; throws std::invalid_argument otherwise.
// The value is the estimated fraction of ALL samples that carry a bit, so an adaptive embedder
// that only touches busy pixels shows up diluted (bits used / total samples).
std::vector<LsbDetection> lsbDetection(const cv::Mat& img);

}  // namespace analysis
