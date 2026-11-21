#pragma once
#include "stego/IStegoAlgorithm.hpp"

#include <cstdint>
#include <span>
#include <vector>

// Shared building blocks for "one slot carries k bits" algorithms (AdaptiveLsb, AudioLsb, ...).
// An algorithm only has to say how many slots it has and how to read/write k bits of slot i;
// the keyed permutation, header exclusion and bit packing live here, once.
namespace stego::slots {

// Slot indices (into [0, slotCount)) that carry `nBytes` of data for `region`, in embedding order.
// Header: first rawHeaderBytes*8/k slots of the permutation seeded by kHeaderSeed.
// Body:   permutation seeded by bodySeed, header slots skipped. Prefix-stable in nBytes.
// rawHeaderBytes is what the header occupies in the carrier: the header size itself, or more if
// the algorithm expands it (BlockDct adds Reed-Solomon parity). Same at embed and extract. Throws std::length_error if there are not enough slots, std::invalid_argument if a
// Header request is not exactly rawHeaderBytes.
std::vector<uint32_t> select(size_t slotCount, int bitsPerSlot, Region region,
                             const Seed32& bodySeed, size_t nBytes,
                             size_t rawHeaderBytes);

// What to write into slot j. Stream bits are taken MSB-first from `data`; the last slot may
// carry fewer than k bits, then only those bits are touched.
//   newValue = (oldValue & ~c.mask) | c.value
struct Chunk {
    unsigned value;
    unsigned mask;
};
Chunk chunk(std::span<const uint8_t> data, size_t j, int bitsPerSlot);

// Reads slot j back: `rawLowBits` = the slot's k low bits as stored. `out` must start zeroed.
void store(std::span<uint8_t> out, size_t j, int bitsPerSlot, unsigned rawLowBits);

// Progress/cancel helper: calls progress every 65536 slots, throws OperationCancelled on false.
void tick(const Progress& progress, size_t j, size_t total);

}  // namespace stego::slots
