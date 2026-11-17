#pragma once
// Envelope scheme (no magic bytes, no plaintext length):
//
//   Header region : [ML-KEM-768 ciphertext, 1088 B]
//                   -> embedded at positions derived from a PUBLIC constant seed
//                      (headerSeed()). The ciphertext is pseudorandom, so
//                      knowing the positions leaks nothing.
//   Body region   : [len(4B) XOR mask][ChaCha20-Poly1305 ciphertext][tag 16B]
//                   -> embedded at positions from a permutation seeded by
//                      Session::bodySeed() = HKDF(shared_secret, "perm").
//
// Everything (key, nonce, length mask, permutation seed) comes from the ML-KEM
// shared secret via HKDF-SHA256 with distinct info labels. The shared secret is
// fresh per encapsulation, so deriving the nonce is safe (key is never reused).
//
// NOTE: ML-KEM uses implicit rejection. A WRONG private key does not fail at
// decapsulation, it yields a random secret. You only find out when the length
// field is garbage or the Poly1305 tag fails. Treat both as "wrong key / no data".

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace crypto {

using Bytes  = std::vector<uint8_t>;
using Seed32 = std::array<uint8_t, 32>;

inline constexpr size_t kKemCiphertextSize = 1088;  // ML-KEM-768
inline constexpr size_t kLengthFieldSize   = 4;
inline constexpr size_t kTagSize           = 16;
inline constexpr size_t kMaxPayload        = size_t{1} << 28;  // 256 MiB sanity cap

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct KeyPair {
    Bytes publicKey;   // DER (SubjectPublicKeyInfo)
    Bytes privateKey;  // DER (PKCS#8); keep it secret, wipe when done
};

KeyPair generateKeyPair();

// Public seed for the header region (SHA-256 of a fixed label).
Seed32 headerSeed();

class Session {
public:
    // Sender side: fills kemCiphertextOut with 1088 bytes to put into the header region.
    static Session encapsulate(std::span<const uint8_t> recipientPublicKeyDer,
                               Bytes& kemCiphertextOut);
    // Receiver side: kemCiphertext is what was extracted from the header region.
    static Session decapsulate(std::span<const uint8_t> privateKeyDer,
                               std::span<const uint8_t> kemCiphertext);

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept                 = default;
    Session& operator=(Session&&) noexcept      = default;
    ~Session();

    const Seed32& bodySeed() const noexcept { return bodySeed_; }

    // payload -> [len^mask (4)] [ciphertext] [tag (16)]
    Bytes sealBody(std::span<const uint8_t> payload) const;

    // Receiver step 1: extract first 4 bytes of the body, call this to get the
    // total body size to extract (4 + len + 16). Throws on absurd length.
    size_t bodySizeFromLengthField(std::span<const uint8_t> lengthField) const;

    // Receiver step 2: full body blob -> plaintext. Throws Error if the tag fails.
    Bytes openBody(std::span<const uint8_t> body) const;

private:
    explicit Session(std::span<const uint8_t> sharedSecret);

    std::array<uint8_t, 32> key_{};
    std::array<uint8_t, 12> nonce_{};
    std::array<uint8_t, 4>  lenMask_{};
    Seed32                  bodySeed_{};
};

}  // namespace crypto
