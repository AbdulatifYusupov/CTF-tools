#include "crypto/Crypto.hpp"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <memory>
#include <string>
#include <string_view>

#if OPENSSL_VERSION_NUMBER < 0x30500000L
#error "OpenSSL >= 3.5 is required (ML-KEM support)"
#endif

namespace crypto {
namespace {

[[noreturn]] void fail(const char* what) {
    char buf[256] = {};
    if (unsigned long e = ERR_get_error()) ERR_error_string_n(e, buf, sizeof buf);
    ERR_clear_error();
    throw Error(std::string(what) + (buf[0] ? std::string(": ") + buf : std::string()));
}

template <auto F>
struct Del {
    template <class T>
    void operator()(T* p) const noexcept { F(p); }
};
using PkeyPtr    = std::unique_ptr<EVP_PKEY, Del<EVP_PKEY_free>>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, Del<EVP_PKEY_CTX_free>>;
using KdfPtr     = std::unique_ptr<EVP_KDF, Del<EVP_KDF_free>>;
using KdfCtxPtr  = std::unique_ptr<EVP_KDF_CTX, Del<EVP_KDF_CTX_free>>;
using CipherPtr  = std::unique_ptr<EVP_CIPHER_CTX, Del<EVP_CIPHER_CTX_free>>;

template <class F>
Bytes serialize(F&& fn) {  // fn(unsigned char**) -> int, i2d_* style
    int n = fn(nullptr);
    if (n <= 0) fail("DER length");
    Bytes out(static_cast<size_t>(n));
    unsigned char* p = out.data();
    if (fn(&p) != n) fail("DER encode");
    return out;
}

void hkdf(std::span<const uint8_t> secret, std::string_view info, std::span<uint8_t> out) {
    KdfPtr kdf(EVP_KDF_fetch(nullptr, "HKDF", nullptr));
    if (!kdf) fail("EVP_KDF_fetch(HKDF)");
    KdfCtxPtr ctx(EVP_KDF_CTX_new(kdf.get()));
    if (!ctx) fail("EVP_KDF_CTX_new");

    char digest[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                          const_cast<uint8_t*>(secret.data()), secret.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                          const_cast<char*>(info.data()), info.size()),
        OSSL_PARAM_construct_end()};
    if (EVP_KDF_derive(ctx.get(), out.data(), out.size(), params) <= 0) fail("HKDF derive");
}

}  // namespace

// ---- Keys --------------------------------------------------------------------

KeyPair generateKeyPair() {
    PkeyPtr key(EVP_PKEY_Q_keygen(nullptr, nullptr, "ML-KEM-768"));
    if (!key) fail("ML-KEM-768 keygen");
    KeyPair kp;
    kp.publicKey  = serialize([&](unsigned char** p) { return i2d_PUBKEY(key.get(), p); });
    kp.privateKey = serialize([&](unsigned char** p) { return i2d_PrivateKey(key.get(), p); });
    return kp;
}

Bytes randomBytes(size_t n) {
    Bytes b(n);
    if (n != 0 && RAND_bytes(b.data(), static_cast<int>(n)) != 1) fail("RAND_bytes");
    return b;
}

// ---- Session -------------------------------------------------------------------

Session::Session(std::span<const uint8_t> ss) {
    hkdf(ss, "stego/v1/key", key_);
    hkdf(ss, "stego/v1/nonce", nonce_);
    hkdf(ss, "stego/v1/len-mask", lenMask_);
    hkdf(ss, "stego/v1/perm-seed", bodySeed_);
}

Session::~Session() {
    OPENSSL_cleanse(key_.data(), key_.size());
    OPENSSL_cleanse(nonce_.data(), nonce_.size());
    OPENSSL_cleanse(lenMask_.data(), lenMask_.size());
    OPENSSL_cleanse(bodySeed_.data(), bodySeed_.size());
}

Session Session::encapsulate(std::span<const uint8_t> pub, Bytes& kemCtOut) {
    const unsigned char* p = pub.data();
    PkeyPtr key(d2i_PUBKEY(nullptr, &p, static_cast<long>(pub.size())));
    if (!key) fail("parse public key");

    PkeyCtxPtr ctx(EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr));
    if (!ctx || EVP_PKEY_encapsulate_init(ctx.get(), nullptr) != 1) fail("encapsulate_init");

    size_t ctLen = 0, ssLen = 0;
    if (EVP_PKEY_encapsulate(ctx.get(), nullptr, &ctLen, nullptr, &ssLen) != 1)
        fail("encapsulate size query");
    if (ctLen != kKemCiphertextSize || ssLen != 32) throw Error("unexpected ML-KEM sizes");

    kemCtOut.assign(ctLen, 0);
    std::array<uint8_t, 32> ss{};
    if (EVP_PKEY_encapsulate(ctx.get(), kemCtOut.data(), &ctLen, ss.data(), &ssLen) != 1)
        fail("encapsulate");

    Session s{ss};
    OPENSSL_cleanse(ss.data(), ss.size());
    return s;
}

Session Session::decapsulate(std::span<const uint8_t> priv, std::span<const uint8_t> kemCt) {
    if (kemCt.size() != kKemCiphertextSize) throw Error("bad KEM ciphertext size");

    const unsigned char* p = priv.data();
    PkeyPtr key(d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(priv.size())));
    if (!key) fail("parse private key");

    PkeyCtxPtr ctx(EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr));
    if (!ctx || EVP_PKEY_decapsulate_init(ctx.get(), nullptr) != 1) fail("decapsulate_init");

    std::array<uint8_t, 32> ss{};
    size_t ssLen = ss.size();
    if (EVP_PKEY_decapsulate(ctx.get(), ss.data(), &ssLen, kemCt.data(), kemCt.size()) != 1)
        fail("decapsulate");

    Session s{std::span<const uint8_t>(ss)};
    OPENSSL_cleanse(ss.data(), ss.size());
    return s;
}

Session Session::fromPassword(std::string_view password, std::span<const uint8_t> salt,
                              const ArgonCost& cost) {
    if (password.empty()) throw Error("empty password");
    if (salt.size() != kSaltSize) throw Error("bad salt size");
    if (cost.memoryKiB < 8 || cost.iterations < 1) throw Error("bad Argon2 cost");

    KdfPtr kdf(EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr));
    if (!kdf) fail("Argon2id not available (needs OpenSSL >= 3.2)");
    KdfCtxPtr ctx(EVP_KDF_CTX_new(kdf.get()));
    if (!ctx) fail("EVP_KDF_CTX_new");

    uint32_t iterations = cost.iterations, memory = cost.memoryKiB, lanes = 1;
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, const_cast<char*>(password.data()),
                                          password.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<uint8_t*>(salt.data()),
                                          salt.size()),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iterations),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memory),
        OSSL_PARAM_construct_end()};

    std::array<uint8_t, 32> secret{};
    if (EVP_KDF_derive(ctx.get(), secret.data(), secret.size(), params) <= 0) fail("Argon2id derive");

    Session s{secret};
    OPENSSL_cleanse(secret.data(), secret.size());
    return s;
}

// ---- Body (AEAD) ---------------------------------------------------------------

Bytes Session::sealBody(std::span<const uint8_t> payload) const {
    if (payload.empty() || payload.size() > kMaxPayload) throw Error("payload size out of range");

    Bytes out(kLengthFieldSize + payload.size() + kTagSize);
    const auto len = static_cast<uint32_t>(payload.size());
    for (size_t i = 0; i < kLengthFieldSize; ++i)
        out[i] = static_cast<uint8_t>(len >> (8 * i)) ^ lenMask_[i];

    CipherPtr c(EVP_CIPHER_CTX_new());
    int n = 0;
    if (!c || EVP_EncryptInit_ex(c.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(nonce_.size()),
                            nullptr) != 1 ||
        EVP_EncryptInit_ex(c.get(), nullptr, nullptr, key_.data(), nonce_.data()) != 1)
        fail("AEAD init");

    // masked length is authenticated as AAD
    if (EVP_EncryptUpdate(c.get(), nullptr, &n, out.data(), kLengthFieldSize) != 1) fail("AEAD aad");
    uint8_t* ct = out.data() + kLengthFieldSize;
    if (EVP_EncryptUpdate(c.get(), ct, &n, payload.data(), static_cast<int>(payload.size())) != 1)
        fail("AEAD encrypt");
    if (EVP_EncryptFinal_ex(c.get(), ct + n, &n) != 1) fail("AEAD final");
    if (EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_AEAD_GET_TAG, kTagSize, ct + payload.size()) != 1)
        fail("AEAD get tag");
    return out;
}

size_t Session::bodySizeFromLengthField(std::span<const uint8_t> field) const {
    if (field.size() != kLengthFieldSize) throw Error("length field must be 4 bytes");
    uint32_t len = 0;
    for (size_t i = 0; i < kLengthFieldSize; ++i)
        len |= static_cast<uint32_t>(field[i] ^ lenMask_[i]) << (8 * i);
    if (len == 0 || len > kMaxPayload) throw Error("invalid length (wrong key or no data)");
    return kLengthFieldSize + len + kTagSize;
}

Bytes Session::openBody(std::span<const uint8_t> body) const {
    const size_t expected = bodySizeFromLengthField(body.first(kLengthFieldSize));
    if (body.size() != expected) throw Error("body size mismatch");
    const size_t ctLen = body.size() - kLengthFieldSize - kTagSize;

    CipherPtr c(EVP_CIPHER_CTX_new());
    int n = 0;
    if (!c || EVP_DecryptInit_ex(c.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(nonce_.size()),
                            nullptr) != 1 ||
        EVP_DecryptInit_ex(c.get(), nullptr, nullptr, key_.data(), nonce_.data()) != 1)
        fail("AEAD init");

    if (EVP_DecryptUpdate(c.get(), nullptr, &n, body.data(), kLengthFieldSize) != 1) fail("AEAD aad");

    Bytes plain(ctLen);
    if (EVP_DecryptUpdate(c.get(), plain.data(), &n, body.data() + kLengthFieldSize,
                          static_cast<int>(ctLen)) != 1)
        fail("AEAD decrypt");
    if (EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_AEAD_SET_TAG, kTagSize,
                            const_cast<uint8_t*>(body.data() + kLengthFieldSize + ctLen)) != 1)
        fail("AEAD set tag");
    if (EVP_DecryptFinal_ex(c.get(), plain.data() + n, &n) <= 0) {
        ERR_clear_error();
        throw Error("authentication failed (wrong key or corrupted data)");
    }
    return plain;
}

}  // namespace crypto
