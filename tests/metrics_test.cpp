#include "analysis/Metrics.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

int main() {
    cv::RNG rng(2024);
    cv::Mat img(128, 128, CV_8UC3);
    rng.fill(img, cv::RNG::UNIFORM, 0, 256);

    // PSNR / SSIM
    CHECK(std::isinf(analysis::psnr(img, img)));
    CHECK(std::fabs(analysis::ssim(img, img) - 1.0) < 1e-9);

    cv::Mat flipped;
    cv::bitwise_xor(img, cv::Scalar::all(1), flipped);  // every channel changes by exactly 1
    CHECK(std::fabs(analysis::psnr(img, flipped) - 48.1308) < 0.01);  // 10*log10(255^2 / 1)
    const double s = analysis::ssim(img, flipped);
    CHECK(s < 1.0 && s > 0.99);

    cv::Mat other(128, 128, CV_8UC3);
    rng.fill(other, cv::RNG::UNIFORM, 0, 256);
    CHECK(analysis::ssim(img, other) < 0.5);

    // Chi-square: even-only values = maximally "combed" histogram -> p ~ 0.
    // Randomising the LSB of every pixel equalises the pairs -> p clearly above 0.
    cv::Mat even(256, 256, CV_8UC3);
    rng.fill(even, cv::RNG::UNIFORM, 0, 128);
    even *= 2;
    const auto before = analysis::chiSquare(even);
    CHECK(before.dof > 100);
    CHECK(before.pEmbedded < 1e-6);

    cv::Mat bits(even.size(), even.type());
    rng.fill(bits, cv::RNG::UNIFORM, 0, 2);
    cv::Mat randomised;
    cv::bitwise_or(even, bits, randomised);
    const auto after = analysis::chiSquare(randomised);
    CHECK(after.pEmbedded > 1e-4);

    // LSB plane
    const cv::Mat plane = analysis::lsbPlane(img, 0);
    cv::Mat expect;
    cv::bitwise_and(img, cv::Scalar::all(1), expect);
    expect *= 255;
    CHECK(cv::norm(plane, expect, cv::NORM_INF) == 0);

    // Amplified diff of a +-1 change is visible and bounded
    const cv::Mat d = analysis::amplifiedDiff(img, flipped, 64.0);
    double mn = 0, mx = 0;
    cv::minMaxLoc(d.reshape(1), &mn, &mx);
    CHECK(mn == 64 && mx == 64);

    // Input validation
    bool threw = false;
    try { (void)analysis::psnr(img, cv::Mat(64, 64, CV_8UC3)); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    std::printf("chi2 even: p=%.3g | randomised LSB: p=%.3g\n", before.pEmbedded, after.pEmbedded);
    std::puts("metrics_test: OK");
    return 0;
}
