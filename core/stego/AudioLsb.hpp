#pragma once
#include "stego/IStegoAlgorithm.hpp"

namespace stego {

// LSB for 16-bit PCM audio (interleaved samples, all channels share one slot space).
//
// Slot = one sample carrying `bitsPerSample` bits. A sample is eligible if
// |sample with its low bits zeroed| >= threshold, so near-silent passages are left alone:
// flipping LSBs in digital silence turns clean zeros into audible-on-analysis noise, which is
// the easiest tell for audio LSB. The test uses the masked value, so it is identical before
// and after embedding (same trick as the image variant).
//
// threshold = 0 means every sample is eligible (classic LSB).
// ~64 is roughly -66 dBFS: below that, passages count as silence.
// Output must be written back losslessly (WAV / FLAC), see AudioIO.
class AudioLsb final : public IStegoAlgorithm {
public:
    struct Options {
        int bitsPerSample = 1;  // 1 or 2
        int threshold     = 64;
    };

    AudioLsb() : AudioLsb(Options{}) {}
    explicit AudioLsb(Options opt);

    std::string name() const override { return "AudioLSB"; }
    size_t capacityBits(const Carrier& carrier, size_t headerBytes) const override;

    void embed(Carrier& carrier, Region region, std::span<const uint8_t> data, const Seed32& seed,
               size_t headerBytes, const Progress& progress = {}) const override;

    std::vector<uint8_t> extract(const Carrier& carrier, Region region, size_t nBytes,
                                 const Seed32& seed, size_t headerBytes,
                                 const Progress& progress = {}) const override;

private:
    Options opt_;
};

}  // namespace stego
