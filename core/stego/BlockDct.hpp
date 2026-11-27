#pragma once
#include "ecc/ReedSolomon.hpp"
#include "stego/IStegoAlgorithm.hpp"

namespace stego {

// Koch-Zhao style block-DCT steganography on the luma of 8-bit gray / BGR / BGRA images.
//
// Every full 8x8 block carries up to `pairsPerBlock` bits. A bit lives in the relation of two
// mid-frequency DCT coefficients:  bit = (|c_a| > |c_b|).  The decoder only compares, so
// `strength` is an ENCODER-ONLY knob: the encoder pushes the pair apart by at least `strength`,
// then simulates the real output (integer rounding, clipping), re-measures, and raises the
// push per block until the relation survives with margin >= strength/2.
//
// Luma is the integer Y = (77R + 150G + 29B + 128) >> 8 (not cv::cvtColor): adding the same
// offset d to R, G and B changes Y by exactly d, so the simulation is exact and independent of
// the OpenCV version. Chroma is never touched.
//
// Slots = all (block, pair) combinations, no content-based filtering. Such a filter would have
// to be identical before and after embedding, and a block-DCT change can't guarantee that.
// Blocks that cannot hold a bit (saturated areas) become bit errors, which Reed-Solomon fixes.
//
// ECC is built in, because DCT embedding is lossy per block and a single wrong bit in the
// ML-KEM header would ruin decapsulation:
//   Header: RS(encode(header))     (1088 B ML-KEM ciphertext, or 16 B Argon2id salt)
//   Body:   RS block of the first 4 bytes (the length field) || RS stream of the rest
// The tiny first block lets the receiver learn the length before it knows how much to read.
//
// capacityBits() reports capacity in ENVELOPE bits (after subtracting header and parity), so
// the pipeline's capacity check stays correct.
//
// strength: for lossless output (PNG) the decoder reads the exact pixels the encoder simulated,
// so little is needed. Measured on synthetic images (smooth to sigma 15, flat saturated patches,
// two-phase embedding, 3 pairs): zero raw bit errors from strength 6 up. Distortion grows quickly
// with it: at ~70% fill, strength 12 / 16 / 24 gave PSNR 44 / 42 / 38 dB and SSIM 0.97 / 0.96 / 0.92.
// Real photos (12 images, password mode, up to 90% of capacity): every extraction was exact at
// strength 8 and 12; 8 gave +2.2 dB median PSNR (+1.6 dB worst case) and ~+0.016 SSIM over 12, hence
// the default of 8. Values below 8 are allowed (4..) but are NOT validated on photos yet; whether
// Reed-Solomon had to correct anything is not visible in those runs.
// Raise it only for extra margin against small later pixel changes (not tested; it does not make
// the payload survive JPEG).
//
// pairsPerBlock and paritySymbols must be identical at embed and extract; strength need not.
// Realistic capacity (pairs=2, parity=32): ~ (W/8)*(H/8)*2 raw bits. The ML-KEM header takes
// ~10 kbit of that (a 1024x768 image then carries ~1.5 KB of payload); the password header
// (16 B salt) takes ~0.4 kbit (~2.9 KB on the same image). Output must stay lossless (PNG).
class BlockDct final : public IStegoAlgorithm {
public:
    struct Options {
        int pairsPerBlock = 2;   // 1..3 bits per block; more = more capacity, more distortion
        int strength      = 8;   // 4..100, encoder-only; see note below
        int paritySymbols = 32;  // RS parity per block, corrects paritySymbols/2 bytes
    };

    BlockDct() : BlockDct(Options{}) {}
    explicit BlockDct(Options opt);

    std::string name() const override { return "BlockDCT"; }
    size_t capacityBits(const Carrier& carrier, size_t headerBytes) const override;

    void embed(Carrier& carrier, Region region, std::span<const uint8_t> data, const Seed32& seed,
               size_t headerBytes, const Progress& progress = {}) const override;

    // Throws std::runtime_error if ECC cannot recover the data ("no data or too damaged").
    std::vector<uint8_t> extract(const Carrier& carrier, Region region, size_t nBytes,
                                 const Seed32& seed, size_t headerBytes,
                                 const Progress& progress = {}) const override;

private:
    size_t rawHeaderBytes(size_t headerBytes) const;
    size_t rawBodyBytes(size_t bodyBytes) const;
    std::vector<uint8_t> packHeader(std::span<const uint8_t> data) const;
    std::vector<uint8_t> packBody(std::span<const uint8_t> data) const;

    Options opt_;
    ecc::ReedSolomon rs_;
};

}  // namespace stego
