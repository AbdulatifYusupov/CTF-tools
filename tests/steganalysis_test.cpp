// RS / sample-pairs estimators on synthetic covers with a KNOWN embedding rate.
// No OpenCV: the estimators work on raw planes. Thresholds were set from measured spread over many
// seeds (see the "worst" lines printed below), not guessed.
#include "analysis/Steganalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <vector>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

using namespace analysis;

namespace {

// Deterministic across standard libraries: only mt19937's raw output (fully specified) is used.
struct Rng {
    std::mt19937 g;
    explicit Rng(uint32_t s) : g(s) {}
    double uni() { return (static_cast<double>(g()) + 0.5) / 4294967296.0; }
    double normal() { return std::sqrt(-2.0 * std::log(uni())) * std::cos(6.283185307179586 * uni()); }
    uint32_t bit() { return g() & 1u; }
};

// Smooth sinusoid field + white Gaussian noise, rounded: a crude stand-in for an unprocessed photo.
std::vector<uint8_t> makeCover(int w, int h, double sigma, uint32_t seed) {
    Rng r(seed);
    double fx[3], fy[3], ph[3];
    for (int i = 0; i < 3; ++i) {
        fx[i] = 0.004 + 0.01 * (i + 1) * (0.5 + r.uni());
        fy[i] = 0.004 + 0.008 * (i + 1) * (0.5 + r.uni());
        ph[i] = 6.283185307179586 * r.uni();
    }
    std::vector<uint8_t> img(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double v = 128;
            for (int i = 0; i < 3; ++i) v += 28 * std::sin(fx[i] * x + fy[i] * y + ph[i]);
            v += sigma * r.normal();
            img[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
        }
    return img;
}

// LSB replacement: each sample carries a random bit with probability beta.
template <class T>
void embed(std::vector<T>& v, double beta, uint32_t seed) {
    Rng r(seed);
    for (auto& s : v)
        if (r.uni() < beta) s = static_cast<T>((s & ~static_cast<T>(1)) | static_cast<T>(r.bit()));
}

struct Worst {
    double rs = 0, spa = 0;
};

constexpr int kW = 512, kH = 512;

Worst worstError(double sigma, double beta, int seeds) {
    Worst w;
    for (int s = 0; s < seeds; ++s) {
        auto img = makeCover(kW, kH, sigma, 100 + static_cast<uint32_t>(s));
        embed(img, beta, 900 + static_cast<uint32_t>(s));
        const Plane8 pl{img, kW, kH, 1, 0};
        const auto a = rsAnalysis(pl);
        const auto b = samplePairs(pl);
        CHECK(a.valid && b.valid);
        w.rs  = std::max(w.rs, std::fabs(a.p - beta));
        w.spa = std::max(w.spa, std::fabs(b.p - beta));
    }
    return w;
}

}  // namespace

int main() {
    constexpr int kSeeds = 12;

    // 1. Clean covers and known rates, three noise levels.
    for (double sigma : {0.6, 1.5, 3.0}) {
        for (double beta : {0.0, 0.1, 0.25, 0.5, 0.75}) {
            const Worst w = worstError(sigma, beta, kSeeds);
            std::printf("sigma %.1f beta %.2f: worst |error| RS %.4f  SPA %.4f\n", sigma, beta, w.rs, w.spa);
            CHECK(w.rs < 0.09);  // measured worst over 12 seeds: 0.066 (sigma 3)
            CHECK(w.spa < 0.09);
            if (sigma <= 1.5) {  // tighter on the less noisy covers (measured worst 0.043)
                CHECK(w.rs < 0.06);
                CHECK(w.spa < 0.06);
            }
        }
    }

    // 2. Full replacement (p = 1): SPA must stay usable (double-root fallback may kick in); RS has a
    //    pole there and is only required not to crash.
    for (int s = 0; s < kSeeds; ++s) {
        auto img = makeCover(kW, kH, 1.5, 300 + static_cast<uint32_t>(s));
        embed(img, 1.0, 700 + static_cast<uint32_t>(s));
        const Plane8 pl{img, kW, kH, 1, 0};
        const auto b = samplePairs(pl);
        CHECK(b.valid && b.p > 0.85 && b.p < 1.1);
        (void)rsAnalysis(pl);
    }

    // 3. Patchy embedding: only the top half carries bits, so the whole-image estimate is p/2.
    {
        auto img = makeCover(kW, kH, 1.5, 11);
        std::vector<uint8_t> top(img.begin(), img.begin() + img.size() / 2);
        embed(top, 1.0, 5);
        std::copy(top.begin(), top.end(), img.begin());
        const Plane8 pl{img, kW, kH, 1, 0};
        CHECK(std::fabs(rsAnalysis(pl).p - 0.5) < 0.05);
        CHECK(std::fabs(samplePairs(pl).p - 0.5) < 0.05);
    }

    // 4. Indexing: interleaved 3-channel image with row padding must give EXACTLY the numbers
    //    of the same channel copied into a contiguous plane.
    {
        const int w = 203, h = 97, pad = 5;  // odd sizes: width not a multiple of 4
        const size_t rowStride = static_cast<size_t>(w) * 3 + pad;
        std::vector<uint8_t> buf(rowStride * h, 0xAA);  // padding = garbage that must never be read
        std::vector<std::vector<uint8_t>> chan(3);
        for (int c = 0; c < 3; ++c) {
            chan[static_cast<size_t>(c)] = makeCover(w, h, 1.5, 40 + static_cast<uint32_t>(c));
            embed(chan[static_cast<size_t>(c)], 0.3, 60 + static_cast<uint32_t>(c));
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    buf[y * rowStride + static_cast<size_t>(x) * 3 + static_cast<size_t>(c)] =
                        chan[static_cast<size_t>(c)][static_cast<size_t>(y) * w + x];
        }
        for (int c = 0; c < 3; ++c) {
            const Plane8 strided{std::span<const uint8_t>(buf).subspan(static_cast<size_t>(c)), static_cast<size_t>(w),
                                 static_cast<size_t>(h), 3, rowStride};
            const Plane8 flat{chan[static_cast<size_t>(c)], static_cast<size_t>(w), static_cast<size_t>(h), 1, 0};
            CHECK(rsAnalysis(strided).p == rsAnalysis(flat).p);
            CHECK(samplePairs(strided).p == samplePairs(flat).p);
            CHECK(rsAnalysis(strided).units == rsAnalysis(flat).units);
            CHECK(samplePairs(strided).units == samplePairs(flat).units);
        }
    }

    // 5. 16-bit plane: identical to the 8-bit result when the data is the same shifted by a constant
    //    (no clamping involved), and sane on interleaved stereo audio-like data with negative samples.
    {
        auto img = makeCover(kW, kH, 1.5, 21);
        embed(img, 0.25, 8);
        std::vector<int16_t> shifted(img.size());
        for (size_t i = 0; i < img.size(); ++i) shifted[i] = static_cast<int16_t>(static_cast<int>(img[i]) - 128);
        const Plane8 p8{img, kW, kH, 1, 0};
        const Plane16 p16{shifted, kW, kH, 1, 0};
        CHECK(rsAnalysis(p8).p == rsAnalysis(p16).p);
        CHECK(samplePairs(p8).p == samplePairs(p16).p);

        const size_t frames = 200000;
        std::vector<int16_t> st(frames * 2);
        Rng r(33);
        double worstRs = 0, worstSpa = 0;
        for (double beta : {0.0, 0.25, 0.5}) {
            for (size_t i = 0; i < frames; ++i)
                for (size_t c = 0; c < 2; ++c) {
                    const double v = 6000 * std::sin(0.0007 * static_cast<double>(i) * static_cast<double>(c + 1) + 1.0) +
                                     2.0 * r.normal();
                    st[i * 2 + c] = static_cast<int16_t>(std::lround(v));
                }
            embed(st, beta, 12);
            for (size_t c = 0; c < 2; ++c) {
                const Plane16 ch{std::span<const int16_t>(st).subspan(c), frames, 1, 2, 0};
                const auto a = rsAnalysis(ch);
                const auto b = samplePairs(ch);
                CHECK(a.valid && b.valid);
                worstRs  = std::max(worstRs, std::fabs(a.p - beta));
                worstSpa = std::max(worstSpa, std::fabs(b.p - beta));
            }
        }
        std::printf("16-bit stereo: worst |error| RS %.4f  SPA %.4f\n", worstRs, worstSpa);
        // Measured: at noise sigma 1-2 the worst error over 8 seeds was ~0.05; it grows quickly for noisier
        // signals (sigma 8: ~0.14), so only a quiet signal is asserted here.
        CHECK(worstRs < 0.09 && worstSpa < 0.09);
    }

    // 6. Degenerate inputs.
    {
        std::vector<uint8_t> flat(64 * 64, 100);
        const Plane8 pf{flat, 64, 64, 1, 0};
        CHECK(std::fabs(rsAnalysis(pf).p) < 1e-9 || !rsAnalysis(pf).valid);  // must not claim an embedding
        CHECK(std::fabs(samplePairs(pf).p) < 1e-9 || !samplePairs(pf).valid);

        std::vector<uint8_t> tiny(8 * 8, 1);
        CHECK(!rsAnalysis(Plane8{tiny, 8, 8, 1, 0}).valid);
        CHECK(!samplePairs(Plane8{tiny, 8, 8, 1, 0}).valid);

        bool threw = false;
        try { (void)rsAnalysis(Plane8{std::span<const uint8_t>(tiny).first(10), 8, 8, 1, 0}); }
        catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { (void)samplePairs(Plane8{tiny, 0, 8, 1, 0}); }
        catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }

    std::puts("steganalysis_test: OK");
    return 0;
}
