#include "analysis/Metrics.hpp"

#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace analysis {
namespace {

void validate1(const cv::Mat& m) {
    if (m.empty() || m.depth() != CV_8U || m.channels() == 2 || m.channels() > 4)
        throw std::invalid_argument("expected an 8-bit gray/BGR/BGRA image");
}

void validate2(const cv::Mat& a, const cv::Mat& b) {
    validate1(a);
    validate1(b);
    if (a.size() != b.size() || a.channels() != b.channels())
        throw std::invalid_argument("images differ in size or channel count");
}

// Drops alpha, so metrics see only colour.
cv::Mat colorPart(const cv::Mat& m) {
    if (m.channels() != 4) return m;
    cv::Mat out;
    cv::cvtColor(m, out, cv::COLOR_BGRA2BGR);
    return out;
}

// Upper regularised incomplete gamma Q(a, x) (series / continued fraction, Numerical Recipes).
double gammaQ(double a, double x) {
    if (x <= 0) return 1.0;
    const double gln = std::lgamma(a);
    if (x < a + 1.0) {
        double ap = a, sum = 1.0 / a, del = sum;
        for (int n = 0; n < 2000; ++n) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * 1e-15) break;
        }
        return 1.0 - sum * std::exp(-x + a * std::log(x) - gln);
    }
    const double tiny = 1e-300;
    double b = x + 1.0 - a, c = 1.0 / tiny, d = 1.0 / b, h = d;
    for (int i = 1; i < 2000; ++i) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < tiny) d = tiny;
        c = b + an / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-15) break;
    }
    return std::exp(-x + a * std::log(x) - gln) * h;
}

}  // namespace

double psnr(const cv::Mat& original, const cv::Mat& stego) {
    validate2(original, stego);
    const cv::Mat a = colorPart(original), b = colorPart(stego);

    cv::Mat d;
    cv::absdiff(a, b, d);
    d.convertTo(d, CV_64F);
    d = d.mul(d);
    const cv::Scalar m = cv::mean(d);
    double mse = 0;
    for (int c = 0; c < a.channels(); ++c) mse += m[c];
    mse /= a.channels();

    if (mse == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

double ssim(const cv::Mat& original, const cv::Mat& stego) {
    validate2(original, stego);
    constexpr int kWin = 11;
    constexpr int kCrop = kWin / 2;
    if (original.rows < kWin || original.cols < kWin)
        throw std::invalid_argument("SSIM needs at least 11x11 pixels");

    const double C1 = (0.01 * 255) * (0.01 * 255);
    const double C2 = (0.03 * 255) * (0.03 * 255);

    const cv::Mat a = colorPart(original), b = colorPart(stego);
    auto blur = [](const cv::Mat& m) {
        cv::Mat o;
        cv::GaussianBlur(m, o, cv::Size(kWin, kWin), 1.5, 1.5, cv::BORDER_REFLECT);
        return o;
    };
    const cv::Rect valid(kCrop, kCrop, a.cols - 2 * kCrop, a.rows - 2 * kCrop);

    double total = 0;
    for (int c = 0; c < a.channels(); ++c) {
        cv::Mat x, y;  // double precision: E[x^2] - mu^2 cancels badly in float
        cv::extractChannel(a, x, c);
        cv::extractChannel(b, y, c);
        x.convertTo(x, CV_64F);
        y.convertTo(y, CV_64F);

        const cv::Mat mu1 = blur(x), mu2 = blur(y);
        const cv::Mat mu11 = mu1.mul(mu1), mu22 = mu2.mul(mu2), mu12 = mu1.mul(mu2);
        const cv::Mat s11 = blur(x.mul(x)) - mu11;
        const cv::Mat s22 = blur(y.mul(y)) - mu22;
        const cv::Mat s12 = blur(x.mul(y)) - mu12;

        const cv::Mat num = (2.0 * mu12 + C1).mul(2.0 * s12 + C2);
        const cv::Mat den = (mu11 + mu22 + C1).mul(s11 + s22 + C2);
        cv::Mat map;
        cv::divide(num, den, map);
        total += cv::mean(map(valid))[0];
    }
    return total / a.channels();
}

ChiSquareResult chiSquare(const cv::Mat& img, int channel) {
    validate1(img);
    const int nColor = img.channels() == 1 ? 1 : 3;
    if (channel < -1 || channel >= nColor) throw std::invalid_argument("bad channel");

    std::array<uint64_t, 256> hist{};
    const int cn = img.channels();
    for (int y = 0; y < img.rows; ++y) {
        const uint8_t* row = img.ptr<uint8_t>(y);
        for (int x = 0; x < img.cols; ++x)
            for (int c = 0; c < nColor; ++c)
                if (channel == -1 || channel == c) ++hist[row[x * cn + c]];
    }

    ChiSquareResult r;
    int pairs = 0;
    for (int i = 0; i < 128; ++i) {
        const double n0 = static_cast<double>(hist[2 * i]);
        const double n1 = static_cast<double>(hist[2 * i + 1]);
        const double e = (n0 + n1) / 2.0;
        if (e < 5.0) continue;  // classic validity rule for chi-square bins
        r.chi2 += (n0 - e) * (n0 - e) / e;
        ++pairs;
    }
    r.dof = pairs - 1;
    r.pEmbedded = r.dof > 0 ? gammaQ(r.dof / 2.0, r.chi2 / 2.0) : 0.0;
    return r;
}

cv::Mat lsbPlane(const cv::Mat& img, int bit) {
    validate1(img);
    if (bit < 0 || bit > 7) throw std::invalid_argument("bit must be 0..7");
    cv::Mat t, out;
    cv::bitwise_and(img, cv::Scalar::all(1 << bit), t);
    t.convertTo(out, CV_8U, 255.0 / (1 << bit));  // exact: power-of-two scale
    return out;
}

cv::Mat amplifiedDiff(const cv::Mat& original, const cv::Mat& stego, double gain) {
    validate2(original, stego);
    cv::Mat d, out;
    cv::absdiff(original, stego, d);
    d.convertTo(out, CV_8U, gain);  // saturates at 255
    return out;
}

}  // namespace analysis
