#include "stego/SlotCodec.hpp"

#include "stego/Prng.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace stego::slots {
namespace {

constexpr size_t kProgressStep = size_t{1} << 16;

size_t ceilDiv(size_t a, size_t b) { return (a + b - 1) / b; }

// Lazy Fisher-Yates: first `want` elements of the permutation seeded by `seed`, skipping slots
// flagged in `skip`. Prefix-stable: asking for fewer gives a prefix of asking for more, which
// the receiver relies on (read the length field first, then the rest).
std::vector<uint32_t> takeFromPermutation(size_t N, const Seed32& seed, size_t want,
                                          const std::vector<bool>* skip) {
    if (want > N) throw std::length_error("carrier too small");
    std::vector<uint32_t> perm(N);
    std::iota(perm.begin(), perm.end(), 0u);
    Prng rng(seed);
    std::vector<uint32_t> out;
    out.reserve(want);
    for (size_t i = 0; i < N && out.size() < want; ++i) {
        const size_t j = i + static_cast<size_t>(rng.below(N - i));
        std::swap(perm[i], perm[j]);
        if (!skip || !(*skip)[perm[i]]) out.push_back(perm[i]);
    }
    if (out.size() < want) throw std::length_error("carrier too small");
    return out;
}

}  // namespace

std::vector<uint32_t> select(size_t N, int k, Region region, const Seed32& bodySeed,
                             size_t nBytes, size_t rawHeaderBytes) {
    if (k < 1 || k > 8) throw std::invalid_argument("bitsPerSlot must be 1..8");
    if (N > std::numeric_limits<uint32_t>::max()) throw std::length_error("too many slots");
    const size_t headerSlots = ceilDiv(rawHeaderBytes * 8, static_cast<size_t>(k));

    if (region == Region::Header) {
        if (nBytes != rawHeaderBytes) throw std::invalid_argument("header size mismatch");
        return takeFromPermutation(N, kHeaderSeed, headerSlots, nullptr);
    }
    const auto hdr = takeFromPermutation(N, kHeaderSeed, headerSlots, nullptr);
    std::vector<bool> skip(N, false);
    for (uint32_t s : hdr) skip[s] = true;
    return takeFromPermutation(N, bodySeed, ceilDiv(nBytes * 8, static_cast<size_t>(k)), &skip);
}

Chunk chunk(std::span<const uint8_t> data, size_t j, int k) {
    const size_t totalBits = data.size() * 8;
    const size_t first     = j * static_cast<size_t>(k);
    const int m            = static_cast<int>(std::min<size_t>(static_cast<size_t>(k), totalBits - first));
    unsigned v = 0;
    for (int b = 0; b < m; ++b) {
        const size_t bit = first + static_cast<size_t>(b);
        v = (v << 1) | ((data[bit >> 3] >> (7 - (bit & 7))) & 1u);
    }
    const unsigned shift = static_cast<unsigned>(k - m);  // partial last slot: top bits of the field
    return {v << shift, ((1u << m) - 1u) << shift};
}

void store(std::span<uint8_t> out, size_t j, int k, unsigned raw) {
    const size_t totalBits = out.size() * 8;
    const size_t first     = j * static_cast<size_t>(k);
    const int m            = static_cast<int>(std::min<size_t>(static_cast<size_t>(k), totalBits - first));
    const unsigned shift   = static_cast<unsigned>(k - m);
    const unsigned field   = (raw >> shift) & ((1u << m) - 1u);
    for (int b = 0; b < m; ++b) {
        const size_t bit = first + static_cast<size_t>(b);
        if ((field >> (m - 1 - b)) & 1u) out[bit >> 3] |= static_cast<uint8_t>(1u << (7 - (bit & 7)));
    }
}

void tick(const Progress& progress, size_t j, size_t total) {
    if (progress && (j % kProgressStep) == 0 && !progress(static_cast<float>(j) / static_cast<float>(total)))
        throw OperationCancelled();
}

}  // namespace stego::slots
