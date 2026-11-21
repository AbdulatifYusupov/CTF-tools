#pragma once
#include "stego/IStegoAlgorithm.hpp"

#include <string_view>

namespace stego {

// Two envelope modes (see crypto/Crypto.hpp). The receiver must know which one was used: nothing in
// the file says so (no magic bytes). Errors: std::length_error if the carrier is too small,
// crypto::Error on crypto failure, std::runtime_error from the algorithm (e.g. Reed-Solomon failed)
// when there is no message / the key or password is wrong / the file was modified.

// ---- KEM mode: recipient key pair, 1088-byte header ------------------------------------------------
void embedMessage(const IStegoAlgorithm& algo, Carrier& carrier, std::span<const uint8_t> payload,
                  std::span<const uint8_t> recipientPublicKeyDer, const Progress& progress = {});

std::vector<uint8_t> extractMessage(const IStegoAlgorithm& algo, const Carrier& carrier,
                                    std::span<const uint8_t> recipientPrivateKeyDer,
                                    const Progress& progress = {});

// ---- Password mode: Argon2id, 16-byte header -------------------------------------------------------
void embedMessageWithPassword(const IStegoAlgorithm& algo, Carrier& carrier,
                              std::span<const uint8_t> payload, std::string_view password,
                              const Progress& progress = {});

std::vector<uint8_t> extractMessageWithPassword(const IStegoAlgorithm& algo, const Carrier& carrier,
                                                std::string_view password,
                                                const Progress& progress = {});

// Largest payload that fits (0 if even the envelope does not).
size_t maxPayloadBytes(const IStegoAlgorithm& algo, const Carrier& carrier, size_t headerBytes);

}  // namespace stego
