#include "stego/AdaptiveLsb.hpp"

#include "stego/SlotCodec.hpp"

#include <opencv2/imgproc.hpp>

#include <cstdlib>
#include <limits>

namespace stego {
namespace {

void validate(const cv::Mat* m) {
    if (!m || m->empty() || m->depth() != CV_8U || m->channels() == 2 || m->channels() > 4)
        throw std::invalid_argument("AdaptiveLSB needs an 8-bit gray/BGR/BGRA image");
}

// Eligible pixels (ascending linear index) and colour channels used per pixel.
struct SlotSpace {
    std::vector<uint32_t> pixels;
    int channels = 1;
    size_t slotCount() const { return pixels.size() * static_cast<size_t>(channels); }
};

SlotSpace buildSpace(const cv::Mat& img, int k, int threshold) {
    // Zero the embedding bits first: the map must not depend on them.
    cv::Mat masked;
    cv::bitwise_and(img, cv::Scalar::all(0xFF & ~((1 << k) - 1)), masked);

    cv::Mat gray;
    switch (img.channels()) {
        case 1: gray = masked; break;
        case 3: cv::cvtColor(masked, gray, cv::COLOR_BGR2GRAY); break;
        default: cv::cvtColor(masked, gray, cv::COLOR_BGRA2GRAY); break;
    }

    cv::Mat gx, gy;  // integer-only path (8U -> 16S) keeps results identical across machines
    cv::Sobel(gray, gx, CV_16S, 1, 0, 3, 1, 0, cv::BORDER_REPLICATE);
    cv::Sobel(gray, gy, CV_16S, 0, 1, 3, 1, 0, cv::BORDER_REPLICATE);

    SlotSpace sp;
    sp.channels = img.channels() == 1 ? 1 : 3;
    const int rows = img.rows, cols = img.cols;
    if (static_cast<uint64_t>(rows) * cols * sp.channels > std::numeric_limits<uint32_t>::max())
        throw std::length_error("image too large");
    for (int y = 0; y < rows; ++y) {
        const int16_t* px = gx.ptr<int16_t>(y);
        const int16_t* py = gy.ptr<int16_t>(y);
        for (int x = 0; x < cols; ++x)
            if (std::abs(px[x]) + std::abs(py[x]) >= threshold)
                sp.pixels.push_back(static_cast<uint32_t>(y) * cols + x);
    }
    return sp;
}

template <class Mat>  // cv::Mat or const cv::Mat
auto* slotPtr(Mat& img, const SlotSpace& sp, uint32_t slot) {
    const uint32_t pix = sp.pixels[slot / static_cast<uint32_t>(sp.channels)];
    const int ch       = static_cast<int>(slot % static_cast<uint32_t>(sp.channels));
    return img.template ptr<uint8_t>(static_cast<int>(pix / static_cast<uint32_t>(img.cols))) +
           (pix % static_cast<uint32_t>(img.cols)) * static_cast<uint32_t>(img.channels()) + ch;
}

}  // namespace

AdaptiveLsb::AdaptiveLsb(Options opt) : opt_(opt) {
    if (opt_.bitsPerChannel < 1 || opt_.bitsPerChannel > 2)
        throw std::invalid_argument("bitsPerChannel must be 1 or 2");
    if (opt_.threshold < 0) throw std::invalid_argument("threshold must be >= 0");
}

size_t AdaptiveLsb::capacityBits(const Carrier& carrier, size_t /*headerBytes*/) const {
    const auto* m = std::get_if<cv::Mat>(&carrier);
    validate(m);
    return buildSpace(*m, opt_.bitsPerChannel, opt_.threshold).slotCount() *
           static_cast<size_t>(opt_.bitsPerChannel);
}

void AdaptiveLsb::embed(Carrier& carrier, Region region, std::span<const uint8_t> data,
                        const Seed32& seed, size_t headerBytes, const Progress& progress) const {
    auto* img = std::get_if<cv::Mat>(&carrier);
    validate(img);
    if (data.empty()) throw std::invalid_argument("nothing to embed");

    const int k    = opt_.bitsPerChannel;
    const auto sp  = buildSpace(*img, k, opt_.threshold);
    const auto sel = slots::select(sp.slotCount(), k, region, seed, data.size(), headerBytes);

    for (size_t j = 0; j < sel.size(); ++j) {
        slots::tick(progress, j, sel.size());
        const auto c = slots::chunk(data, j, k);
        uint8_t* p   = slotPtr(*img, sp, sel[j]);
        *p = static_cast<uint8_t>((static_cast<unsigned>(*p) & ~c.mask) | c.value);
    }
    if (progress) progress(1.0f);
}

std::vector<uint8_t> AdaptiveLsb::extract(const Carrier& carrier, Region region, size_t nBytes,
                                          const Seed32& seed, size_t headerBytes,
                                          const Progress& progress) const {
    const auto* img = std::get_if<cv::Mat>(&carrier);
    validate(img);

    const int k    = opt_.bitsPerChannel;
    const auto sp  = buildSpace(*img, k, opt_.threshold);
    const auto sel = slots::select(sp.slotCount(), k, region, seed, nBytes, headerBytes);

    std::vector<uint8_t> out(nBytes, 0);
    const unsigned lowMask = (1u << k) - 1u;
    for (size_t j = 0; j < sel.size(); ++j) {
        slots::tick(progress, j, sel.size());
        slots::store(out, j, k, static_cast<unsigned>(*slotPtr(*img, sp, sel[j])) & lowMask);
    }
    if (progress) progress(1.0f);
    return out;
}

}  // namespace stego
