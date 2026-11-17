#pragma once
#include "stego/IStegoAlgorithm.hpp"

namespace stego {

// Adaptive LSB for 8-bit gray / BGR / BGRA images (alpha is never touched).
//
// Slot = one colour channel of one "busy" pixel, carrying `bitsPerChannel` bits.
// A pixel is busy if |Sobel_x| + |Sobel_y| of the gray image >= threshold.
//
// The complexity map is computed on the image with the low `bitsPerChannel` bits
// zeroed in every channel, using integer-only OpenCV ops. Embedding only changes
// those bits, so the map is bit-identical before and after embedding, and the
// extractor walks exactly the same slots.
//
// threshold: Sobel 3x3 gives ~8 per gray level of slope, so 32 is roughly
// "4+ levels per pixel". Lower = more capacity, less adaptive.
//
// Carrier note: embed() modifies the cv::Mat in place (cv::Mat copies share
// pixels). clone() the original first if you want a before/after comparison.
// Input must come from a lossless source; decode JPEG, then SAVE AS PNG.
class AdaptiveLsb final : public IStegoAlgorithm {
public:
    struct Options {
        int bitsPerChannel = 1;  // 1 or 2
        int threshold      = 32;
    };

    AdaptiveLsb() : AdaptiveLsb(Options{}) {}
    explicit AdaptiveLsb(Options opt);

    std::string name() const override { return "AdaptiveLSB"; }
    size_t capacityBits(const Carrier& carrier) const override;

    void embed(Carrier& carrier, Region region, std::span<const uint8_t> data, const Seed32& seed,
               const Progress& progress = {}) const override;

    std::vector<uint8_t> extract(const Carrier& carrier, Region region, size_t nBytes,
                                 const Seed32& seed,
                                 const Progress& progress = {}) const override;

private:
    Options opt_;
};

}  // namespace stego
