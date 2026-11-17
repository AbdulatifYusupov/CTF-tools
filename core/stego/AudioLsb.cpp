#include "stego/AudioLsb.hpp"

#include "stego/SlotCodec.hpp"

#include <cstdlib>
#include <limits>

namespace stego {
namespace {

void validate(const AudioData* a) {
    if (!a || a->samples.empty() || a->channels < 1)
        throw std::invalid_argument("AudioLSB needs non-empty 16-bit PCM audio");
}

// Indices of eligible samples. Uses the value with the embedding bits cleared, so the list
// is the same before and after embedding.
std::vector<uint32_t> eligible(const AudioData& a, int k, int threshold) {
    if (a.samples.size() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("audio too long");
    const int clear = ~((1 << k) - 1);
    std::vector<uint32_t> idx;
    idx.reserve(a.samples.size());
    for (size_t i = 0; i < a.samples.size(); ++i)
        if (std::abs(static_cast<int>(a.samples[i]) & clear) >= threshold)
            idx.push_back(static_cast<uint32_t>(i));
    return idx;
}

}  // namespace

AudioLsb::AudioLsb(Options opt) : opt_(opt) {
    if (opt_.bitsPerSample < 1 || opt_.bitsPerSample > 2)
        throw std::invalid_argument("bitsPerSample must be 1 or 2");
    if (opt_.threshold < 0) throw std::invalid_argument("threshold must be >= 0");
}

size_t AudioLsb::capacityBits(const Carrier& carrier) const {
    const auto* a = std::get_if<AudioData>(&carrier);
    validate(a);
    return eligible(*a, opt_.bitsPerSample, opt_.threshold).size() *
           static_cast<size_t>(opt_.bitsPerSample);
}

void AudioLsb::embed(Carrier& carrier, Region region, std::span<const uint8_t> data,
                     const Seed32& seed, const Progress& progress) const {
    auto* a = std::get_if<AudioData>(&carrier);
    validate(a);
    if (data.empty()) throw std::invalid_argument("nothing to embed");

    const int k      = opt_.bitsPerSample;
    const auto elig  = eligible(*a, k, opt_.threshold);
    const auto sel   = slots::select(elig.size(), k, region, seed, data.size());

    for (size_t j = 0; j < sel.size(); ++j) {
        slots::tick(progress, j, sel.size());
        const auto c   = slots::chunk(data, j, k);
        int16_t& s     = a->samples[elig[sel[j]]];
        const auto u   = static_cast<uint16_t>(s);
        s = static_cast<int16_t>(static_cast<uint16_t>((u & ~c.mask) | c.value));
    }
    if (progress) progress(1.0f);
}

std::vector<uint8_t> AudioLsb::extract(const Carrier& carrier, Region region, size_t nBytes,
                                       const Seed32& seed, const Progress& progress) const {
    const auto* a = std::get_if<AudioData>(&carrier);
    validate(a);

    const int k     = opt_.bitsPerSample;
    const auto elig = eligible(*a, k, opt_.threshold);
    const auto sel  = slots::select(elig.size(), k, region, seed, nBytes);

    std::vector<uint8_t> out(nBytes, 0);
    const unsigned lowMask = (1u << k) - 1u;
    for (size_t j = 0; j < sel.size(); ++j) {
        slots::tick(progress, j, sel.size());
        const auto u = static_cast<uint16_t>(a->samples[elig[sel[j]]]);
        slots::store(out, j, k, static_cast<unsigned>(u) & lowMask);
    }
    if (progress) progress(1.0f);
    return out;
}

}  // namespace stego
