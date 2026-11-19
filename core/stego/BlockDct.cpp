#include "stego/BlockDct.hpp"

#include "stego/SlotCodec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stego {
namespace {

using Vec64 = std::array<double, 64>;

// Coefficient pairs (row*8 + col). bit 1 <=> |a| > |b|. All mid-frequency.
struct CoefPair {
    int a, b;
};
constexpr CoefPair kPairs[3] = {{4 * 8 + 3, 3 * 8 + 4}, {5 * 8 + 2, 2 * 8 + 5}, {3 * 8 + 5, 5 * 8 + 3}};

// ---- orthonormal 8x8 DCT-II (own implementation: no dependence on OpenCV internals) -------------

const std::array<double, 64>& basis() {  // D[k*8 + n]
    static const auto m = [] {
        std::array<double, 64> d{};
        const double pi = std::acos(-1.0);
        for (int k = 0; k < 8; ++k)
            for (int n = 0; n < 8; ++n)
                d[static_cast<size_t>(k * 8 + n)] =
                    (k == 0 ? std::sqrt(1.0 / 8.0) : std::sqrt(2.0 / 8.0)) *
                    std::cos((2 * n + 1) * k * pi / 16.0);
        return d;
    }();
    return m;
}

Vec64 dct2(const Vec64& x) {  // C = D X D^T
    const auto& D = basis();
    Vec64 tmp{}, out{};
    for (int k = 0; k < 8; ++k)
        for (int n = 0; n < 8; ++n) {
            double s = 0;
            for (int m = 0; m < 8; ++m) s += D[static_cast<size_t>(k * 8 + m)] * x[static_cast<size_t>(m * 8 + n)];
            tmp[static_cast<size_t>(k * 8 + n)] = s;
        }
    for (int k = 0; k < 8; ++k)
        for (int l = 0; l < 8; ++l) {
            double s = 0;
            for (int n = 0; n < 8; ++n) s += tmp[static_cast<size_t>(k * 8 + n)] * D[static_cast<size_t>(l * 8 + n)];
            out[static_cast<size_t>(k * 8 + l)] = s;
        }
    return out;
}

Vec64 idct2(const Vec64& c) {  // X = D^T C D
    const auto& D = basis();
    Vec64 tmp{}, out{};
    for (int m = 0; m < 8; ++m)
        for (int n = 0; n < 8; ++n) {
            double s = 0;
            for (int k = 0; k < 8; ++k) s += D[static_cast<size_t>(k * 8 + m)] * c[static_cast<size_t>(k * 8 + n)];
            tmp[static_cast<size_t>(m * 8 + n)] = s;
        }
    for (int m = 0; m < 8; ++m)
        for (int l = 0; l < 8; ++l) {
            double s = 0;
            for (int n = 0; n < 8; ++n) s += tmp[static_cast<size_t>(m * 8 + n)] * D[static_cast<size_t>(n * 8 + l)];
            out[static_cast<size_t>(m * 8 + l)] = s;
        }
    return out;
}

// ---- image access ----------------------------------------------------------------------------------

void validate(const cv::Mat* m) {
    if (!m || m->empty() || m->depth() != CV_8U ||
        (m->channels() != 1 && m->channels() != 3 && m->channels() != 4) || m->rows < 8 || m->cols < 8)
        throw std::invalid_argument("BlockDCT needs an 8-bit gray/BGR/BGRA image of at least 8x8");
}

struct Geometry {
    int blocksX = 0, blocksY = 0;
    size_t blocks = 0, slotCount = 0;
};

Geometry geometry(const cv::Mat& m, int pairs) {
    Geometry g;
    g.blocksX   = m.cols / 8;
    g.blocksY   = m.rows / 8;
    g.blocks    = static_cast<size_t>(g.blocksX) * static_cast<size_t>(g.blocksY);
    g.slotCount = g.blocks * static_cast<size_t>(pairs);
    if (g.slotCount > std::numeric_limits<uint32_t>::max()) throw std::length_error("image too large");
    return g;
}

template <class M>  // cv::Mat or const cv::Mat
auto* pix(M& m, int x, int y) {
    return m.template ptr<uint8_t>(y) + static_cast<size_t>(x) * static_cast<size_t>(m.channels());
}

int lumaOf(const uint8_t* p, int cn) {  // BGR order
    return cn == 1 ? p[0] : (77 * p[2] + 150 * p[1] + 29 * p[0] + 128) >> 8;
}

using PixBlock = std::array<std::array<uint8_t, 4>, 64>;

template <class M>
void loadBlock(M& img, int bx, int by, PixBlock& px, Vec64& luma) {
    const int cn = img.channels();
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            const uint8_t* p = pix(img, bx * 8 + c, by * 8 + r);
            const size_t i   = static_cast<size_t>(r * 8 + c);
            for (int ch = 0; ch < cn; ++ch) px[i][static_cast<size_t>(ch)] = p[ch];
            luma[i] = lumaOf(p, cn);
        }
}

// ---- per-block embedding ---------------------------------------------------------------------------

double marginOf(const Vec64& co, int pair, int bit) {
    const double d = std::fabs(co[static_cast<size_t>(kPairs[pair].a)]) - std::fabs(co[static_cast<size_t>(kPairs[pair].b)]);
    return bit ? d : -d;
}

// Pushes the pair so that the wanted relation holds with margin D, signs preserved.
void pushPair(Vec64& t, int pair, int bit, double D) {
    const size_t ia = static_cast<size_t>(kPairs[pair].a), ib = static_cast<size_t>(kPairs[pair].b);
    const double sa = t[ia] < 0 ? -1.0 : 1.0, sb = t[ib] < 0 ? -1.0 : 1.0;
    const double mean = (std::fabs(t[ia]) + std::fabs(t[ib])) / 2.0;
    const double hi   = std::max(mean + D / 2.0, D);
    const double lo   = std::max(mean - D / 2.0, 0.0);
    t[ia] = sa * (bit ? hi : lo);
    t[ib] = sb * (bit ? lo : hi);
}

// want[p] = -1 (leave pair alone), 0 or 1.
void embedBlock(cv::Mat& img, int bx, int by, const std::array<int8_t, 3>& want, int pairs, int strength) {
    PixBlock orig{};
    Vec64 y0{};
    loadBlock(img, bx, by, orig, y0);
    const Vec64 c0 = dct2(y0);

    bool needs = false;
    for (int p = 0; p < pairs; ++p)
        if (want[static_cast<size_t>(p)] >= 0 && marginOf(c0, p, want[static_cast<size_t>(p)]) < strength) needs = true;
    if (!needs) return;  // natural relation already right and strong enough: leave untouched

    const int cn = img.channels(), cc = cn == 1 ? 1 : 3;  // alpha is never modified
    const double minMargin = std::max(2.0, strength / 2.0);
    const int maxStrength  = strength * 3;

    PixBlock best = orig;
    double bestWorst = -std::numeric_limits<double>::infinity();

    for (int D = strength; D <= maxStrength; D += 4) {
        Vec64 t = c0;
        for (int p = 0; p < pairs; ++p) {
            const int w = want[static_cast<size_t>(p)];
            if (w >= 0 && marginOf(c0, p, w) < D) pushPair(t, p, w, D);
        }
        const Vec64 yt = idct2(t);

        PixBlock cand = orig;
        Vec64 y2{};
        for (size_t i = 0; i < 64; ++i) {
            const int d = static_cast<int>(std::lround(yt[i] - y0[i]));
            for (size_t ch = 0; ch < static_cast<size_t>(cc); ++ch)
                cand[i][ch] = static_cast<uint8_t>(std::clamp(static_cast<int>(orig[i][ch]) + d, 0, 255));
            y2[i] = lumaOf(cand[i].data(), cn);  // exactly what the decoder will compute
        }
        const Vec64 c2 = dct2(y2);

        double worst = std::numeric_limits<double>::infinity();
        for (int p = 0; p < pairs; ++p)
            if (want[static_cast<size_t>(p)] >= 0) worst = std::min(worst, marginOf(c2, p, want[static_cast<size_t>(p)]));

        if (worst > bestWorst) {
            bestWorst = worst;
            best      = cand;
        }
        if (worst >= minMargin) break;
    }

    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            uint8_t* p = pix(img, bx * 8 + c, by * 8 + r);
            for (int ch = 0; ch < cc; ++ch) p[ch] = best[static_cast<size_t>(r * 8 + c)][static_cast<size_t>(ch)];
        }
}

struct Assign {
    uint32_t block;
    uint32_t j;  // index in the raw bit stream
    uint8_t pair;
    uint8_t bit;
};

std::vector<Assign> assignments(const std::vector<uint32_t>& sel, std::span<const uint8_t> raw, int pairs) {
    std::vector<Assign> as;
    as.reserve(sel.size());
    for (size_t j = 0; j < sel.size(); ++j) {
        const uint8_t bit = raw.empty() ? 0 : static_cast<uint8_t>(slots::chunk(raw, j, 1).value);
        as.push_back({sel[j] / static_cast<uint32_t>(pairs), static_cast<uint32_t>(j),
                      static_cast<uint8_t>(sel[j] % static_cast<uint32_t>(pairs)), bit});
    }
    std::sort(as.begin(), as.end(), [](const Assign& l, const Assign& r) { return l.block < r.block; });
    return as;
}

void report(const Progress& progress, size_t groupIdx, size_t done, size_t total) {
    if (progress && (groupIdx & 511u) == 0 && !progress(static_cast<float>(done) / static_cast<float>(total)))
        throw OperationCancelled();
}

}  // namespace

// ---- BlockDct ----------------------------------------------------------------------------------------

BlockDct::BlockDct(Options opt) : opt_(opt), rs_(opt.paritySymbols) {
    if (opt_.pairsPerBlock < 1 || opt_.pairsPerBlock > 3) throw std::invalid_argument("pairsPerBlock must be 1..3");
    if (opt_.strength < 8 || opt_.strength > 100) throw std::invalid_argument("strength must be 8..100");
    if (opt_.paritySymbols < 2 || opt_.paritySymbols > 100) throw std::invalid_argument("paritySymbols must be 2..100");
}

size_t BlockDct::rawHeaderBytes() const { return rs_.encodedSize(kHeaderBytes); }

size_t BlockDct::rawBodyBytes(size_t bodyBytes) const {
    const size_t first = 4 + static_cast<size_t>(rs_.parity());  // RS block of the length field
    return first + (bodyBytes > 4 ? rs_.encodedSize(bodyBytes - 4) : 0);
}

std::vector<uint8_t> BlockDct::packHeader(std::span<const uint8_t> data) const {
    if (data.size() != kHeaderBytes) throw std::invalid_argument("header must be kHeaderBytes");
    return rs_.encode(data);
}

std::vector<uint8_t> BlockDct::packBody(std::span<const uint8_t> data) const {
    if (data.size() < 4) throw std::invalid_argument("body must be at least 4 bytes");
    std::vector<uint8_t> raw = rs_.encodeBlock(data.first(4));
    if (data.size() > 4) {
        const auto rest = rs_.encode(data.subspan(4));
        raw.insert(raw.end(), rest.begin(), rest.end());
    }
    return raw;
}

size_t BlockDct::capacityBits(const Carrier& carrier) const {
    const auto* img = std::get_if<cv::Mat>(&carrier);
    validate(img);
    const size_t slotsTotal = geometry(*img, opt_.pairsPerBlock).slotCount;
    const size_t hdrBits    = rawHeaderBytes() * 8;
    if (slotsTotal <= hdrBits) return 0;

    const size_t availBytes = (slotsTotal - hdrBits) / 8;
    const size_t fixed      = 4 + static_cast<size_t>(rs_.parity());
    if (availBytes <= fixed) return 0;
    const size_t avail = availBytes - fixed;

    // Largest r with encodedSize(r) <= avail. encodedSize(r) >= r*255/maxData, so this bounds r.
    size_t r = avail * static_cast<size_t>(rs_.maxBlockData()) / 255;
    while (r > 0 && rs_.encodedSize(r) > avail) --r;
    return (kHeaderBytes + 4 + r) * 8;
}

void BlockDct::embed(Carrier& carrier, Region region, std::span<const uint8_t> data, const Seed32& seed,
                     const Progress& progress) const {
    auto* img = std::get_if<cv::Mat>(&carrier);
    validate(img);
    if (data.empty()) throw std::invalid_argument("nothing to embed");

    const int pairs = opt_.pairsPerBlock;
    const auto g    = geometry(*img, pairs);
    const auto raw  = region == Region::Header ? packHeader(data) : packBody(data);
    const auto sel  = slots::select(g.slotCount, 1, region, seed, raw.size(), rawHeaderBytes());
    const auto as   = assignments(sel, raw, pairs);

    size_t groupIdx = 0;
    for (size_t i = 0; i < as.size(); ++groupIdx) {
        std::array<int8_t, 3> want{-1, -1, -1};
        size_t k = i;
        while (k < as.size() && as[k].block == as[i].block) {
            want[as[k].pair] = static_cast<int8_t>(as[k].bit);
            ++k;
        }
        report(progress, groupIdx, i, as.size());
        embedBlock(*img, static_cast<int>(as[i].block % static_cast<uint32_t>(g.blocksX)),
                   static_cast<int>(as[i].block / static_cast<uint32_t>(g.blocksX)), want, pairs, opt_.strength);
        i = k;
    }
    if (progress) progress(1.0f);
}

std::vector<uint8_t> BlockDct::extract(const Carrier& carrier, Region region, size_t nBytes, const Seed32& seed,
                                       const Progress& progress) const {
    const auto* img = std::get_if<cv::Mat>(&carrier);
    validate(img);
    if (nBytes == 0) return {};

    const int pairs = opt_.pairsPerBlock;
    const auto g    = geometry(*img, pairs);
    const size_t rawBytes =
        region == Region::Header ? rawHeaderBytes() : rawBodyBytes(nBytes);
    if (region == Region::Header && nBytes != kHeaderBytes) throw std::invalid_argument("header must be kHeaderBytes");

    const auto sel = slots::select(g.slotCount, 1, region, seed, rawBytes, rawHeaderBytes());
    const auto as  = assignments(sel, {}, pairs);

    std::vector<uint8_t> raw(rawBytes, 0);
    size_t groupIdx = 0;
    for (size_t i = 0; i < as.size(); ++groupIdx) {
        const int bx = static_cast<int>(as[i].block % static_cast<uint32_t>(g.blocksX));
        const int by = static_cast<int>(as[i].block / static_cast<uint32_t>(g.blocksX));
        PixBlock px{};
        Vec64 y{};
        loadBlock(*img, bx, by, px, y);
        const Vec64 c = dct2(y);

        size_t k = i;
        while (k < as.size() && as[k].block == as[i].block) {
            const auto& pr = kPairs[as[k].pair];
            const unsigned bit = std::fabs(c[static_cast<size_t>(pr.a)]) > std::fabs(c[static_cast<size_t>(pr.b)]) ? 1u : 0u;
            slots::store(raw, as[k].j, 1, bit);
            ++k;
        }
        report(progress, groupIdx, i, as.size());
        i = k;
    }
    if (progress) progress(1.0f);

    ecc::DecodeStats st;
    if (region == Region::Header) {
        auto out = rs_.decode(raw, kHeaderBytes, &st);
        if (!st.ok()) throw std::runtime_error("no valid data found (ECC failed on header)");
        return out;
    }

    const size_t first = 4 + static_cast<size_t>(rs_.parity());
    std::vector<uint8_t> lenBlock(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(first));
    if (!rs_.decodeBlock(lenBlock)) throw std::runtime_error("no valid data found (ECC failed on length field)");

    std::vector<uint8_t> out(lenBlock.begin(), lenBlock.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(nBytes, 4)));
    if (nBytes > 4) {
        const auto rest = rs_.decode(std::span<const uint8_t>(raw).subspan(first), nBytes - 4, &st);
        if (!st.ok()) throw std::runtime_error("ECC failed: image too damaged or wrong parameters");
        out.insert(out.end(), rest.begin(), rest.end());
    }
    return out;
}

}  // namespace stego
