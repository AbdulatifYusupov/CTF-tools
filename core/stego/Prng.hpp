#pragma once
#include "stego/IStegoAlgorithm.hpp"

#include <memory>

namespace stego {

// Deterministic CSPRNG: ChaCha20 keystream keyed with a 32-byte seed, zero IV.
// Same seed -> same sequence on every platform (output is assembled little-endian).
// Shared by all algorithms that need a keyed permutation of embedding slots.
class Prng {
public:
    explicit Prng(const Seed32& seed);
    ~Prng();
    Prng(const Prng&)            = delete;
    Prng& operator=(const Prng&) = delete;

    uint64_t next64();
    uint64_t below(uint64_t bound);  // uniform in [0, bound), bound > 0, no modulo bias

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace stego
