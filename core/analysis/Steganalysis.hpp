#pragma once
// LSB-replacement steganalysis that works on raw sample planes (no OpenCV), so it can be
// unit-tested and run under sanitizers without any imaging dependency.
//
//  * rsAnalysis:    Fridrich, Goljan, Du, "Detecting LSB steganography in color and gray-scale
//                   images" (2001). Groups of 4, mask [0 1 1 0].
//  * samplePairs:   Dumitrescu, Wu, Wang, "Detection of LSB steganography via sample pair
//                   analysis" (2003).
//
// Both return the estimated relative message length p (fraction of samples that carry a bit;
// 0 = clean, 1 = every LSB replaced). The value is RAW: it can be slightly negative or above 1
// from estimation noise. Samples at the extreme values (0/1 and 254/255 for 8-bit) are ignored
// because clipping breaks the model and makes clean noisy images read as embedded. Both are known to be unreliable on flat / synthetic / heavily
// processed covers and on previously JPEG-compressed images.
#include <cstddef>
#include <cstdint>
#include <span>

namespace analysis {

template <class T>
struct Plane {
    std::span<const T> data;  // must cover the last sample that is read
    size_t width     = 0;     // samples per row
    size_t height    = 0;     // rows (audio: 1)
    size_t step      = 1;     // elements between horizontal neighbours (= channels when interleaved)
    size_t rowStride = 0;     // elements between row starts; 0 means width * step
};

using Plane8  = Plane<uint8_t>;
using Plane16 = Plane<int16_t>;

struct LsbEstimate {
    double p      = 0;      // estimated relative message length
    bool valid    = false;  // false: degenerate input (too few groups/pairs, no usable root, ...)
    bool approx   = false;  // valid, but the quadratic had no real root and the double-root fallback was
                            // used. Happens by noise when p is close to 1 (the two roots merge there).
    size_t units  = 0;      // groups (RS) or pairs (SPA) that were used
    bool lowReliability = false;  // many samples were clipped (see skipped): the value can be biased upward
    size_t skipped = 0;     // groups/pairs left out because they touch the first/last 2-value block (clipping)
};

// Throw std::invalid_argument if the span does not cover the described plane.
LsbEstimate rsAnalysis(const Plane8& plane);
LsbEstimate rsAnalysis(const Plane16& plane);
LsbEstimate samplePairs(const Plane8& plane);
LsbEstimate samplePairs(const Plane16& plane);

}  // namespace analysis
