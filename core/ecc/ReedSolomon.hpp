#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Reed-Solomon over GF(2^8) (primitive polynomial 0x11D, generator alpha = 2, first root alpha^0).
//
// A block is `data || parity` with n <= 255 symbols. With `nsym` parity symbols it corrects
//   e errors and f erasures whenever 2e + f <= nsym  (i.e. nsym/2 unknown errors).
//
// Stream API: data is split into B equal-size blocks and the encoded symbols are INTERLEAVED
// (block j, symbol i goes to position i*B + j). A burst of up to B*nsym/2 consecutive wrong
// symbols then costs each block at most nsym/2 errors. The decoder derives the layout from
// (dataLen, nsym), so nothing besides dataLen has to be stored.
namespace ecc {

struct DecodeStats {
    size_t blocks           = 0;
    size_t correctedSymbols = 0;
    size_t failedBlocks     = 0;  // uncorrectable: their bytes are returned as received
    bool ok() const { return failedBlocks == 0; }
};

class ReedSolomon {
public:
    // paritySymbols in [1, 254]. Typical: 16 (corrects 8 bytes/block), 32 (16 bytes/block).
    explicit ReedSolomon(int paritySymbols);

    int parity() const noexcept { return nsym_; }
    int maxBlockData() const noexcept { return 255 - nsym_; }

    // ---- single block ------------------------------------------------------------
    // data.size() in [1, maxBlockData()]. Returns data followed by nsym parity symbols.
    std::vector<uint8_t> encodeBlock(std::span<const uint8_t> data) const;

    // Corrects `block` in place (block.size() in [nsym+1, 255]). `erasures` = indices of
    // symbols known to be unreliable. Returns the number of corrected symbols, or nullopt if
    // the block is uncorrectable (then it is left unchanged).
    std::optional<int> decodeBlock(std::span<uint8_t> block,
                                   std::span<const int> erasures = {}) const;

    // ---- stream ---------------------------------------------------------------------
    size_t encodedSize(size_t dataLen) const;  // dataLen >= 1
    std::vector<uint8_t> encode(std::span<const uint8_t> data) const;

    // Returns exactly dataLen bytes. Check stats->ok(): failed blocks are NOT corrected.
    std::vector<uint8_t> decode(std::span<const uint8_t> encoded, size_t dataLen,
                                DecodeStats* stats = nullptr) const;

private:
    struct Layout {
        size_t blocks;
        size_t dataPerBlock;
        size_t blockLen;
    };
    Layout layout(size_t dataLen) const;

    int nsym_;
    std::vector<uint8_t> gen_;  // generator polynomial, highest degree first, monic
};

}  // namespace ecc
