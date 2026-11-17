#include "stego/Pipeline.hpp"

#include "crypto/Crypto.hpp"

namespace stego {

static_assert(kHeaderBytes == crypto::kKemCiphertextSize,
              "stego header size must match the ML-KEM-768 ciphertext size");

namespace {

// Maps an algorithm's own 0..1 progress into [lo, hi] of the whole operation,
// so header + body report one monotone sequence.
Progress subRange(const Progress& p, float lo, float hi) {
    if (!p) return {};
    return [p, lo, hi](float f) { return p(lo + (hi - lo) * f); };
}

}  // namespace

void embedMessage(const IStegoAlgorithm& algo, Carrier& carrier,
                  std::span<const uint8_t> payload,
                  std::span<const uint8_t> recipientPublicKeyDer, const Progress& progress) {
    crypto::Bytes kemCt;
    const auto session = crypto::Session::encapsulate(recipientPublicKeyDer, kemCt);
    const auto body    = session.sealBody(payload);

    const size_t needBits = (kemCt.size() + body.size()) * 8;
    if (needBits > algo.capacityBits(carrier))
        throw std::length_error("carrier too small: need " + std::to_string(needBits) +
                                " bits, have " + std::to_string(algo.capacityBits(carrier)));

    const float h = static_cast<float>(kemCt.size()) / static_cast<float>(kemCt.size() + body.size());

    // Header first: body embedding excludes the header slots (see interface contract).
    algo.embed(carrier, Region::Header, kemCt, kHeaderSeed, subRange(progress, 0.f, h));
    algo.embed(carrier, Region::Body, body, session.bodySeed(), subRange(progress, h, 1.f));
}

std::vector<uint8_t> extractMessage(const IStegoAlgorithm& algo, const Carrier& carrier,
                                    std::span<const uint8_t> recipientPrivateKeyDer,
                                    const Progress& progress) {
    const auto kemCt = algo.extract(carrier, Region::Header, kHeaderBytes, kHeaderSeed,
                                    subRange(progress, 0.f, 0.05f));
    const auto session = crypto::Session::decapsulate(recipientPrivateKeyDer, kemCt);

    // Length field is 4 bytes: no progress reporting needed.
    const auto lenField =
        algo.extract(carrier, Region::Body, crypto::kLengthFieldSize, session.bodySeed());
    const size_t total = session.bodySizeFromLengthField(lenField);

    // Wrong key -> garbage length. Reject before allocating/reading gigabytes.
    if ((kHeaderBytes + total) * 8 > algo.capacityBits(carrier))
        throw crypto::Error("invalid length (wrong key or no data)");

    const auto body = algo.extract(carrier, Region::Body, total, session.bodySeed(),
                                   subRange(progress, 0.05f, 1.f));
    return session.openBody(body);
}

}  // namespace stego
