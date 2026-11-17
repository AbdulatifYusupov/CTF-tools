#pragma once
#include "stego/IStegoAlgorithm.hpp"

namespace stego {

// payload -> ML-KEM encapsulation -> AEAD body -> embed (header + body).
// Throws std::length_error if the carrier is too small, crypto::Error on crypto failure.
void embedMessage(const IStegoAlgorithm& algo, Carrier& carrier,
                  std::span<const uint8_t> payload,
                  std::span<const uint8_t> recipientPublicKeyDer,
                  const Progress& progress = {});

// Inverse. Throws crypto::Error on wrong key / no data / tampering.
std::vector<uint8_t> extractMessage(const IStegoAlgorithm& algo, const Carrier& carrier,
                                    std::span<const uint8_t> recipientPrivateKeyDer,
                                    const Progress& progress = {});

}  // namespace stego
