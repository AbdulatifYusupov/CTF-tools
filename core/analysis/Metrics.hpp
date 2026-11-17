#pragma once
#include <opencv2/core.hpp>

namespace analysis {

// All functions take 8-bit gray / BGR / BGRA images. Alpha is ignored.
// Throw std::invalid_argument on size/type mismatch.

// PSNR in dB over all colour channels. Returns +infinity for identical images.
// Rule of thumb: LSB-1 on every channel gives ~51 dB; < 40 dB is usually visible.
double psnr(const cv::Mat& original, const cv::Mat& stego);

// Mean SSIM (Wang et al.): Gaussian window 11x11, sigma 1.5, K1=0.01, K2=0.03, L=255,
// computed on the "valid" region (5 px border cropped), averaged over colour channels.
// Needs at least 11x11 pixels. 1.0 = identical.
double ssim(const cv::Mat& original, const cv::Mat& stego);

struct ChiSquareResult {
    double chi2 = 0;       // statistic over pairs of values (2i, 2i+1)
    int dof = 0;           // used pairs - 1 (pairs with expected count < 5 are dropped)
    double pEmbedded = 0;  // P(X >= chi2) under "pairs are equalised": ~1 => LSBs look
                           // randomised (embedding likely), ~0 => natural histogram
};

// Westfeld-Pfitzmann chi-square attack on the whole image.
// channel = -1 pools all colour channels, otherwise 0..2 (B, G, R) or 0 for gray.
// CAVEAT: this is a global test. Adaptive/shuffled embedding touches only busy pixels,
// so at low embedding rates it will NOT fire. That is the point of comparing it against
// sequential LSB, not a bug.
ChiSquareResult chiSquare(const cv::Mat& img, int channel = -1);

// Extracts bit plane `bit` (0 = LSB) as a 0/255 image with the same channel count.
cv::Mat lsbPlane(const cv::Mat& img, int bit = 0);

// |original - stego| multiplied by `gain` and saturated to 8 bit, for the compare view.
cv::Mat amplifiedDiff(const cv::Mat& original, const cv::Mat& stego, double gain = 64.0);

}  // namespace analysis
