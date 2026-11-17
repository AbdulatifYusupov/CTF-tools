#include "ecc/ReedSolomon.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace ecc {
namespace {

// ---- GF(256) ---------------------------------------------------------------------------

struct Gf {
    std::array<uint8_t, 512> exp{};  // doubled so exp[log a + log b] needs no modulo
    std::array<uint16_t, 256> log{};
    Gf() {
        unsigned x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[static_cast<size_t>(i)] = static_cast<uint8_t>(x);
            log[x] = static_cast<uint16_t>(i);
            x <<= 1;
            if (x & 0x100u) x ^= 0x11Du;
        }
        for (size_t i = 255; i < 512; ++i) exp[i] = exp[i - 255];
    }
};

const Gf& gf() {
    static const Gf g;
    return g;
}

uint8_t mul(uint8_t a, uint8_t b) {
    if (!a || !b) return 0;
    const Gf& g = gf();
    return g.exp[static_cast<size_t>(g.log[a] + g.log[b])];
}

uint8_t divide(uint8_t a, uint8_t b) {  // b != 0
    if (!a) return 0;
    const Gf& g = gf();
    return g.exp[static_cast<size_t>(g.log[a] + 255 - g.log[b])];
}

uint8_t alphaPow(int k) {  // alpha^k, any integer k
    k %= 255;
    if (k < 0) k += 255;
    return gf().exp[static_cast<size_t>(k)];
}

// ---- polynomials (lowest degree first unless stated) ---------------------------------------

std::vector<uint8_t> polyMul(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> r(a.size() + b.size() - 1, 0);
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j) r[i + j] ^= mul(a[i], b[j]);
    return r;
}

uint8_t polyEval(const std::vector<uint8_t>& p, uint8_t x) {
    uint8_t r = 0;
    for (size_t i = p.size(); i-- > 0;) r = static_cast<uint8_t>(mul(r, x) ^ p[i]);
    return r;
}

// Evaluates a codeword (first symbol = highest power) at x.
uint8_t evalCodeword(std::span<const uint8_t> c, uint8_t x) {
    uint8_t r = 0;
    for (uint8_t s : c) r = static_cast<uint8_t>(mul(r, x) ^ s);
    return r;
}

std::vector<uint8_t> syndromes(std::span<const uint8_t> c, int nsym) {
    std::vector<uint8_t> s(static_cast<size_t>(nsym));
    for (int j = 0; j < nsym; ++j) s[static_cast<size_t>(j)] = evalCodeword(c, alphaPow(j));
    return s;
}

bool allZero(const std::vector<uint8_t>& v) {
    return std::all_of(v.begin(), v.end(), [](uint8_t b) { return b == 0; });
}

// Berlekamp-Massey: shortest LFSR (connection polynomial C, C[0] = 1) generating u.
std::vector<uint8_t> berlekampMassey(const std::vector<uint8_t>& u, int& Lout) {
    std::vector<uint8_t> C{1}, B{1};
    int L = 0, m = 1;
    uint8_t b = 1;
    for (int n = 0; n < static_cast<int>(u.size()); ++n) {
        uint8_t d = u[static_cast<size_t>(n)];
        for (int i = 1; i <= L && static_cast<size_t>(i) < C.size(); ++i)
            d ^= mul(C[static_cast<size_t>(i)], u[static_cast<size_t>(n - i)]);
        if (d == 0) {
            ++m;
            continue;
        }
        const uint8_t coef = divide(d, b);
        const std::vector<uint8_t> prevC = C;
        if (C.size() < B.size() + static_cast<size_t>(m)) C.resize(B.size() + static_cast<size_t>(m), 0);
        for (size_t i = 0; i < B.size(); ++i) C[i + static_cast<size_t>(m)] ^= mul(coef, B[i]);
        if (2 * L <= n) {
            L = n + 1 - L;
            B = prevC;
            b = d;
            m = 1;
        } else {
            ++m;
        }
    }
    Lout = L;
    return C;
}

}  // namespace

// ---- ReedSolomon ---------------------------------------------------------------------------

ReedSolomon::ReedSolomon(int paritySymbols) : nsym_(paritySymbols) {
    if (nsym_ < 1 || nsym_ > 254) throw std::invalid_argument("parity symbols must be 1..254");
    gen_ = {1};  // highest first: g(x) = prod_{i<nsym} (x + alpha^i)
    for (int i = 0; i < nsym_; ++i) {
        const uint8_t a = alphaPow(i);
        std::vector<uint8_t> next(gen_.size() + 1, 0);
        for (size_t j = 0; j < next.size(); ++j) {
            if (j < gen_.size()) next[j] ^= gen_[j];
            if (j > 0) next[j] ^= mul(gen_[j - 1], a);
        }
        gen_ = std::move(next);
    }
}

std::vector<uint8_t> ReedSolomon::encodeBlock(std::span<const uint8_t> data) const {
    if (data.empty() || data.size() > static_cast<size_t>(maxBlockData()))
        throw std::invalid_argument("block data size out of range");

    std::vector<uint8_t> parity(static_cast<size_t>(nsym_), 0);  // remainder of data*x^nsym mod g
    for (uint8_t d : data) {
        const uint8_t fb = static_cast<uint8_t>(d ^ parity[0]);
        std::copy(parity.begin() + 1, parity.end(), parity.begin());
        parity.back() = 0;
        if (fb)
            for (size_t i = 0; i < parity.size(); ++i) parity[i] ^= mul(gen_[i + 1], fb);
    }
    std::vector<uint8_t> out(data.begin(), data.end());
    out.insert(out.end(), parity.begin(), parity.end());
    return out;
}

std::optional<int> ReedSolomon::decodeBlock(std::span<uint8_t> block,
                                            std::span<const int> erasures) const {
    const int n = static_cast<int>(block.size());
    if (n <= nsym_ || n > 255) throw std::invalid_argument("block size out of range");

    const std::vector<uint8_t> S = syndromes(block, nsym_);
    if (allZero(S)) return 0;

    const int nu = static_cast<int>(erasures.size());
    if (nu > nsym_) return std::nullopt;
    {
        std::vector<bool> seen(static_cast<size_t>(n), false);
        for (int p : erasures) {
            if (p < 0 || p >= n || seen[static_cast<size_t>(p)])
                throw std::invalid_argument("bad erasure position");
            seen[static_cast<size_t>(p)] = true;
        }
    }

    // Erasure locator Gamma(x) = prod (1 + X_k x), X_k = alpha^(n-1-pos)
    std::vector<uint8_t> gamma{1};
    for (int p : erasures) gamma = polyMul(gamma, {1, alphaPow(n - 1 - p)});

    // Forney syndromes: T = S*Gamma mod x^nsym; terms j >= nu behave like error-only syndromes.
    std::vector<uint8_t> T = polyMul(S, gamma);
    T.resize(static_cast<size_t>(nsym_), 0);
    const std::vector<uint8_t> u(T.begin() + nu, T.end());

    int L = 0;
    std::vector<uint8_t> lambda = berlekampMassey(u, L);
    if (2 * L > nsym_ - nu) return std::nullopt;  // more errors than the code can correct
    lambda.resize(static_cast<size_t>(L) + 1, 0);

    const std::vector<uint8_t> psi = polyMul(lambda, gamma);  // full locator, degree L + nu
    const int degree = L + nu;

    // Chien search: position p is a root iff psi(alpha^-(n-1-p)) == 0
    std::vector<int> locs;
    for (int p = 0; p < n; ++p)
        if (polyEval(psi, alphaPow(-(n - 1 - p))) == 0) locs.push_back(p);
    if (static_cast<int>(locs.size()) != degree) return std::nullopt;

    // Forney: Y_k = X_k * Omega(X_k^-1) / psi'(X_k^-1), Omega = S*psi mod x^nsym
    std::vector<uint8_t> omega = polyMul(S, psi);
    omega.resize(static_cast<size_t>(nsym_), 0);
    std::vector<uint8_t> dpsi(psi.size() > 1 ? psi.size() - 1 : 1, 0);  // formal derivative (char 2)
    for (size_t i = 1; i < psi.size(); i += 2) dpsi[i - 1] = psi[i];

    const std::vector<uint8_t> original(block.begin(), block.end());
    for (int p : locs) {
        const int e = n - 1 - p;
        const uint8_t xinv = alphaPow(-e);
        const uint8_t den  = polyEval(dpsi, xinv);
        if (den == 0) {
            std::copy(original.begin(), original.end(), block.begin());
            return std::nullopt;
        }
        const uint8_t mag = mul(alphaPow(e), divide(polyEval(omega, xinv), den));
        block[static_cast<size_t>(p)] ^= mag;
    }

    if (!allZero(syndromes(block, nsym_))) {  // miscorrection guard
        std::copy(original.begin(), original.end(), block.begin());
        return std::nullopt;
    }
    return degree;
}

// ---- stream --------------------------------------------------------------------------------

ReedSolomon::Layout ReedSolomon::layout(size_t dataLen) const {
    if (dataLen == 0) throw std::invalid_argument("empty data");
    const size_t maxData = static_cast<size_t>(maxBlockData());
    const size_t blocks  = (dataLen + maxData - 1) / maxData;
    const size_t per     = (dataLen + blocks - 1) / blocks;  // balanced blocks
    return {blocks, per, per + static_cast<size_t>(nsym_)};
}

size_t ReedSolomon::encodedSize(size_t dataLen) const {
    const auto l = layout(dataLen);
    return l.blocks * l.blockLen;
}

std::vector<uint8_t> ReedSolomon::encode(std::span<const uint8_t> data) const {
    const auto l = layout(data.size());
    std::vector<uint8_t> out(l.blocks * l.blockLen);
    std::vector<uint8_t> chunk(l.dataPerBlock);
    for (size_t j = 0; j < l.blocks; ++j) {
        std::fill(chunk.begin(), chunk.end(), uint8_t{0});  // zero padding on the last block
        const size_t begin = j * l.dataPerBlock;
        if (begin < data.size()) {
            const size_t len = std::min(l.dataPerBlock, data.size() - begin);
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(begin), len, chunk.begin());
        }
        const auto block = encodeBlock(chunk);
        for (size_t i = 0; i < l.blockLen; ++i) out[i * l.blocks + j] = block[i];  // interleave
    }
    return out;
}

std::vector<uint8_t> ReedSolomon::decode(std::span<const uint8_t> encoded, size_t dataLen,
                                         DecodeStats* stats) const {
    const auto l = layout(dataLen);
    if (encoded.size() != l.blocks * l.blockLen)
        throw std::invalid_argument("encoded size does not match dataLen");

    DecodeStats st;
    st.blocks = l.blocks;
    std::vector<uint8_t> data(l.blocks * l.dataPerBlock);
    std::vector<uint8_t> block(l.blockLen);
    for (size_t j = 0; j < l.blocks; ++j) {
        for (size_t i = 0; i < l.blockLen; ++i) block[i] = encoded[i * l.blocks + j];  // deinterleave
        if (const auto fixed = decodeBlock(block))
            st.correctedSymbols += static_cast<size_t>(*fixed);
        else
            ++st.failedBlocks;
        std::copy_n(block.begin(), l.dataPerBlock,
                    data.begin() + static_cast<std::ptrdiff_t>(j * l.dataPerBlock));
    }
    data.resize(dataLen);
    if (stats) *stats = st;
    return data;
}

}  // namespace ecc
