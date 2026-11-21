#pragma once
#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace stego {

using Seed32 = std::array<uint8_t, 32>;

// Header region sizes of the two envelope modes. The algorithms do not care which mode is
// used: they get the header size as a parameter. (static_asserts live in Pipeline.cpp so stego/
// stays independent of crypto/.)
inline constexpr size_t kKemHeaderBytes      = 1088;  // ML-KEM-768 ciphertext
inline constexpr size_t kPasswordHeaderBytes = 16;    // Argon2id salt

// Fixed, PUBLIC seed for header slot positions. Not secret on purpose: the header is an
// ML-KEM ciphertext (pseudorandom) and must be locatable before any key is known.
// Body algorithms need it too, to exclude header slots from the body permutation.
inline constexpr Seed32 kHeaderSeed = {'s', 't', 'e', 'g', 'o', '/', 'v', '1', '/', 'h', 'e',
                                       'a', 'd', 'e', 'r', '-', 'p', 'o', 's', 'i', 't', 'i',
                                       'o', 'n', 's', 0,   0,   0,   0,   0,   0,   0};

struct AudioData {
    std::vector<int16_t> samples;  // interleaved
    int sampleRate = 0;
    int channels   = 0;
    int sfFormat   = 0;  // libsndfile format, to write the file back identically
};

// Image: 8-bit BGR/BGRA cv::Mat (decode JPEG -> save result as PNG, never re-save as JPEG).
using Carrier = std::variant<cv::Mat, AudioData>;

// Header: first `headerBytes`, positions from a public constant seed.
// Body:   everything else, positions from a secret seed.
enum class Region { Header, Body };

// fraction in [0,1]; return false to request cancellation.
using Progress = std::function<bool(float)>;

class OperationCancelled : public std::runtime_error {
public:
    OperationCancelled() : std::runtime_error("operation cancelled") {}
};

class IStegoAlgorithm {
public:
    virtual ~IStegoAlgorithm() = default;

    virtual std::string name() const = 0;

    // Total usable bits for this carrier (header + body) for a header of `headerBytes`.
    // Must not depend on the data being embedded and must be identical at embed and extract time.
    // Plain LSB algorithms ignore headerBytes; BlockDct needs it because its header is RS-expanded.
    virtual size_t capacityBits(const Carrier& carrier, size_t headerBytes) const = 0;

    // CONTRACT (what makes the envelope scheme work):
    //  1. The algorithm builds the list of eligible slots deterministically from
    //     the carrier alone. For adaptive LSB the complexity map is computed on
    //     values with the embedding bits masked out, so it is identical after
    //     embedding.
    //  2. Region::Header (exactly `headerBytes` long) uses the first headerBytes*8 bits' worth of slots of the
    //     permutation seeded by kHeaderSeed (the `seed` argument is ignored).
    //  3. Region::Body uses a permutation seeded by `seed` over the eligible
    //     slots EXCLUDING the header's slots (the algorithm recomputes them
    //     from kHeaderSeed).
    //  4. extract() is prefix-stable: extract(n) is a prefix of extract(m) for n < m
    //     (the receiver first reads 4 bytes of the length field, then the rest).
    //  5. Embed Header before Body, or make sure the header slots are excluded
    //     either way. Throw std::invalid_argument on wrong carrier type,
    //     std::length_error on insufficient capacity, OperationCancelled on cancel.
    //  6. `headerBytes` is the same for the Header and the Body call of one message: the Body
    //     call needs it to know which slots the header occupies.
    virtual void embed(Carrier& carrier, Region region, std::span<const uint8_t> data,
                       const Seed32& seed, size_t headerBytes,
                       const Progress& progress = {}) const = 0;

    virtual std::vector<uint8_t> extract(const Carrier& carrier, Region region, size_t nBytes,
                                         const Seed32& seed, size_t headerBytes,
                                         const Progress& progress = {}) const = 0;
};

}  // namespace stego
