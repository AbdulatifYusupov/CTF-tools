#include "stego/Pipeline.hpp"

#include "crypto/Crypto.hpp"

namespace stego {

static_assert(kKemHeaderBytes == crypto::kKemCiphertextSize, "KEM header size mismatch");
static_assert(kPasswordHeaderBytes == crypto::kSaltSize, "password header size mismatch");

namespace {

// Maps an algorithm's own 0..1 progress into [lo, hi] of the whole operation,
// so header + body report one monotone sequence.
Progress subRange(const Progress& p, float lo, float hi) {
    if (!p) return {};
    return [p, lo, hi](float f) { return p(lo + (hi - lo) * f); };
}

void embedEnvelope(const IStegoAlgorithm& algo, Carrier& carrier, std::span<const uint8_t> header,
                   const crypto::Session& session, std::span<const uint8_t> payload,
                   const Progress& progress) {
    const auto body = session.sealBody(payload);

    const size_t needBits = (header.size() + body.size()) * 8;
    const size_t capacity = algo.capacityBits(carrier, header.size());
    if (needBits > capacity)
        throw std::length_error("carrier too small: need " + std::to_string(needBits) + " bits, have " +
                                std::to_string(capacity));

    const float h = static_cast<float>(header.size()) / static_cast<float>(header.size() + body.size());

    // Header first: body embedding excludes the header slots (see interface contract).
    algo.embed(carrier, Region::Header, header, kHeaderSeed, header.size(), subRange(progress, 0.f, h));
    algo.embed(carrier, Region::Body, body, session.bodySeed(), header.size(), subRange(progress, h, 1.f));
}

// makeSession(header) must return a crypto::Session by value.
template <class MakeSession>
std::vector<uint8_t> extractEnvelope(const IStegoAlgorithm& algo, const Carrier& carrier,
                                     size_t headerBytes, MakeSession&& makeSession,
                                     const Progress& progress) {
    const auto header = algo.extract(carrier, Region::Header, headerBytes, kHeaderSeed, headerBytes,
                                     subRange(progress, 0.f, 0.05f));
    const auto session = makeSession(header);

    // Length field is 4 bytes: no progress reporting needed.
    const auto lenField = algo.extract(carrier, Region::Body, crypto::kLengthFieldSize,
                                       session.bodySeed(), headerBytes);
    const size_t total = session.bodySizeFromLengthField(lenField);

    // Wrong key -> garbage length. Reject before allocating/reading gigabytes.
    if ((headerBytes + total) * 8 > algo.capacityBits(carrier, headerBytes))
        throw crypto::Error("invalid length (wrong key or no data)");

    const auto body = algo.extract(carrier, Region::Body, total, session.bodySeed(), headerBytes,
                                   subRange(progress, 0.05f, 1.f));
    return session.openBody(body);
}

}  // namespace

void embedMessage(const IStegoAlgorithm& algo, Carrier& carrier, std::span<const uint8_t> payload,
                  std::span<const uint8_t> recipientPublicKeyDer, const Progress& progress) {
    crypto::Bytes kemCt;
    const auto session = crypto::Session::encapsulate(recipientPublicKeyDer, kemCt);
    embedEnvelope(algo, carrier, kemCt, session, payload, progress);
}

std::vector<uint8_t> extractMessage(const IStegoAlgorithm& algo, const Carrier& carrier,
                                    std::span<const uint8_t> recipientPrivateKeyDer,
                                    const Progress& progress) {
    return extractEnvelope(
        algo, carrier, kKemHeaderBytes,
        [&](const std::vector<uint8_t>& kemCt) {
            return crypto::Session::decapsulate(recipientPrivateKeyDer, kemCt);
        },
        progress);
}

void embedMessageWithPassword(const IStegoAlgorithm& algo, Carrier& carrier,
                              std::span<const uint8_t> payload, std::string_view password,
                              const Progress& progress) {
    const auto salt    = crypto::randomBytes(crypto::kSaltSize);
    const auto session = crypto::Session::fromPassword(password, salt);
    embedEnvelope(algo, carrier, salt, session, payload, progress);
}

std::vector<uint8_t> extractMessageWithPassword(const IStegoAlgorithm& algo, const Carrier& carrier,
                                                std::string_view password, const Progress& progress) {
    return extractEnvelope(
        algo, carrier, kPasswordHeaderBytes,
        [&](const std::vector<uint8_t>& salt) { return crypto::Session::fromPassword(password, salt); },
        progress);
}

size_t maxPayloadBytes(const IStegoAlgorithm& algo, const Carrier& carrier, size_t headerBytes) {
    const size_t overhead = headerBytes + crypto::kLengthFieldSize + crypto::kTagSize;
    const size_t bytes    = algo.capacityBits(carrier, headerBytes) / 8;
    return bytes > overhead ? bytes - overhead : 0;
}

}  // namespace stego
