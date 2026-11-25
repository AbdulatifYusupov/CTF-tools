#include "analysis/Steganalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace analysis {
namespace {

template <class T> struct Traits;
template <> struct Traits<uint8_t> {
    static constexpr int kMax = 255;
    static int toU(uint8_t v) { return v; }
};
template <> struct Traits<int16_t> {
    static constexpr int kMax = 65535;
    static int toU(int16_t v) { return static_cast<int>(v) + 32768; }  // LSB is preserved
};

// Samples in the first and last 2-value block (0/1 and 254/255, or 0/1 and 65534/65535) are left
// out. Clipping piles samples up on these values, which breaks the parity symmetry both estimators
// rely on; on noisy covers even ~1% of clipped pixels produced estimates of 0.3..0.8 on CLEAN images
// (measured). Membership of a block is unchanged by LSB replacement, so dropping the whole block
// keeps the estimators' model intact. This does NOT remove the bias completely (clean synthetic
// covers with 3% / 9% / 19% of samples clipped still read +0.1 / +0.2 / +0.3), and excluding wider
// margins made it worse, so heavily clipped input is flagged via LsbEstimate::lowReliability.
template <class T>
inline bool atEdge(int u) {
    return (u >> 1) == 0 || (u >> 1) == (Traits<T>::kMax >> 1);
}

template <class T>
const T* rowPtr(const Plane<T>& p, size_t y) {
    const size_t rs = p.rowStride ? p.rowStride : p.width * p.step;
    return p.data.data() + y * rs;
}

template <class T>
void validate(const Plane<T>& p) {
    if (p.width == 0 || p.height == 0 || p.step == 0) throw std::invalid_argument("empty plane");
    const size_t rs = p.rowStride ? p.rowStride : p.width * p.step;
    if (p.height > 1 && rs < (p.width - 1) * p.step + 1) throw std::invalid_argument("row stride too small");
    const size_t need = (p.height - 1) * rs + (p.width - 1) * p.step + 1;
    if (p.data.size() < need) throw std::invalid_argument("span does not cover the plane");
}

enum class Root { None, Real, DoubleRoot };

// Smaller-|x| real root of a x^2 + b x + c = 0. If the discriminant is negative, returns the
// double root -b/2a (the caller decides whether that is plausible).
Root smallRoot(double a, double b, double c, double& x) {
    constexpr double kEps = 1e-12;
    if (std::fabs(a) < kEps) {
        if (std::fabs(b) < kEps) return Root::None;
        x = -c / b;
        return Root::Real;
    }
    const double disc = b * b - 4 * a * c;
    if (disc < 0) {
        x = -b / (2 * a);
        return Root::DoubleRoot;
    }
    const double s  = std::sqrt(disc);
    const double r1 = (-b + s) / (2 * a), r2 = (-b - s) / (2 * a);
    x = std::fabs(r1) < std::fabs(r2) ? r1 : r2;
    return Root::Real;
}

// Both estimators have a double root near p = 1, so noise there can make the discriminant
// negative. Accept the fallback only in that regime; a negative discriminant at small p means
// the cover violates the model (flat, synthetic, saturated, ...) and is reported as invalid.
constexpr double kFallbackMinP = 0.7;

// Share of units dropped because of clipping above which the estimate is flagged. Thresholds are
// from the synthetic clipping sweep: pairs drop about 2x and groups about 4x the clipped sample
// share, and a bias above the estimator's own noise (~0.05) appeared around 3% clipped samples.
constexpr double kPairSkipLimit  = 0.05;
constexpr double kGroupSkipLimit = 0.10;

// ---- RS ----------------------------------------------------------------------------------------

struct RsCounts {
    double rM = 0, sM = 0, rMinus = 0, sMinus = 0;  // groups
};

inline int f1(int v) { return v ^ 1; }
template <int Max>
inline int fMinus1(int v) {  // -1<->0, 1<->2, ...; out-of-range results are clamped (identity at the ends)
    const int r = ((v + 1) ^ 1) - 1;
    return std::clamp(r, 0, Max);
}

inline int disc(const int (&g)[4]) {
    return std::abs(g[1] - g[0]) + std::abs(g[2] - g[1]) + std::abs(g[3] - g[2]);
}

template <class T>
RsCounts rsCounts(const Plane<T>& p, bool flipAll, size_t& groups, size_t& skipped) {
    constexpr int kMax = Traits<T>::kMax;
    RsCounts c;
    groups  = 0;
    skipped = 0;
    for (size_t y = 0; y < p.height; ++y) {
        const T* row = rowPtr(p, y);
        for (size_t x = 0; x + 4 <= p.width; x += 4) {
            int g[4];
            bool edge = false;
            for (int i = 0; i < 4; ++i) {
                g[i] = Traits<T>::toU(row[(x + static_cast<size_t>(i)) * p.step]);
                edge = edge || atEdge<T>(g[i]);
                if (flipAll) g[i] ^= 1;
            }
            if (edge) {
                ++skipped;
                continue;
            }
            const int f0 = disc(g);
            int gm[4] = {g[0], f1(g[1]), f1(g[2]), g[3]};
            int gn[4] = {g[0], fMinus1<kMax>(g[1]), fMinus1<kMax>(g[2]), g[3]};
            const int fm = disc(gm), fn = disc(gn);
            if (fm > f0) ++c.rM; else if (fm < f0) ++c.sM;
            if (fn > f0) ++c.rMinus; else if (fn < f0) ++c.sMinus;
            ++groups;
        }
    }
    return c;
}

template <class T>
LsbEstimate rsImpl(const Plane<T>& p) {
    validate(p);
    size_t n0 = 0, n1 = 0, sk0 = 0, sk1 = 0;
    const RsCounts a = rsCounts(p, false, n0, sk0);
    const RsCounts b = rsCounts(p, true, n1, sk1);
    LsbEstimate r;
    r.units   = n0;
    r.skipped = sk0;
    r.lowReliability = static_cast<double>(sk0) > kGroupSkipLimit * static_cast<double>(n0 + sk0);
    if (n0 < 100) return r;
    const double n   = static_cast<double>(n0);
    const double d0  = (a.rM - a.sM) / n, dm0 = (a.rMinus - a.sMinus) / n;
    const double d1  = (b.rM - b.sM) / n, dm1 = (b.rMinus - b.sMinus) / n;
    // 2(d1+d0) x^2 + (d-0 - d-1 - d1 - 3 d0) x + (d0 - d-0) = 0 ; p = x / (x - 1/2)
    double x = 0;
    const Root kind = smallRoot(2 * (d1 + d0), dm0 - dm1 - d1 - 3 * d0, d0 - dm0, x);
    if (kind == Root::None || std::fabs(x - 0.5) < 1e-9) return r;
    r.p = x / (x - 0.5);
    if (kind == Root::DoubleRoot) {
        if (r.p < kFallbackMinP) return r;
        r.approx = true;
    }
    r.valid = true;
    return r;
}

// ---- Sample pairs ---------------------------------------------------------------------------------

template <class T>
LsbEstimate spaImpl(const Plane<T>& p) {
    validate(p);
    double P = 0, X = 0, Y = 0, K = 0;
    size_t skipped = 0;
    for (size_t y = 0; y < p.height; ++y) {
        const T* row = rowPtr(p, y);
        for (size_t x = 0; x + 1 < p.width; ++x) {
            const int u = Traits<T>::toU(row[x * p.step]);
            const int v = Traits<T>::toU(row[(x + 1) * p.step]);
            if (atEdge<T>(u) || atEdge<T>(v)) {
                ++skipped;
                continue;
            }
            ++P;
            if ((u >> 1) == (v >> 1)) ++K;  // same 2-block: invariant under LSB replacement
            const bool vEven = (v & 1) == 0;
            if (u != v) {
                if ((vEven && u < v) || (!vEven && u > v)) ++X;
                else ++Y;
            }
        }
    }
    LsbEstimate r;
    r.units   = static_cast<size_t>(P);
    r.skipped = skipped;
    r.lowReliability = static_cast<double>(skipped) > kPairSkipLimit * (P + static_cast<double>(skipped));
    if (P < 100) return r;
    // (K/2) p^2 + (2X - P) p + (Y - X) = 0, smaller root
    double x = 0;
    const Root kind = smallRoot(K / 2 / P, (2 * X - P) / P, (Y - X) / P, x);
    if (kind == Root::None) return r;
    r.p = x;
    if (kind == Root::DoubleRoot) {
        if (r.p < kFallbackMinP) return r;
        r.approx = true;
    }
    r.valid = true;
    return r;
}

}  // namespace

LsbEstimate rsAnalysis(const Plane8& p) { return rsImpl(p); }
LsbEstimate rsAnalysis(const Plane16& p) { return rsImpl(p); }
LsbEstimate samplePairs(const Plane8& p) { return spaImpl(p); }
LsbEstimate samplePairs(const Plane16& p) { return spaImpl(p); }

}  // namespace analysis
