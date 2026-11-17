#include "stego/Prng.hpp"

#include <openssl/evp.h>

#include <array>
#include <stdexcept>

namespace stego {

struct Prng::Impl {
    struct Del {
        void operator()(EVP_CIPHER_CTX* c) const noexcept { EVP_CIPHER_CTX_free(c); }
    };
    std::unique_ptr<EVP_CIPHER_CTX, Del> ctx{EVP_CIPHER_CTX_new()};
    std::array<uint8_t, 4096> zeros{};
    std::array<uint8_t, 4096> buf{};
    size_t pos = buf.size();  // forces a refill on first use
};

Prng::Prng(const Seed32& seed) : impl_(std::make_unique<Impl>()) {
    static constexpr uint8_t iv[16] = {};
    if (!impl_->ctx ||
        EVP_EncryptInit_ex(impl_->ctx.get(), EVP_chacha20(), nullptr, seed.data(), iv) != 1)
        throw std::runtime_error("ChaCha20 init failed");
}

Prng::~Prng() = default;

uint64_t Prng::next64() {
    auto& s = *impl_;
    if (s.pos + 8 > s.buf.size()) {
        int n = 0;
        if (EVP_EncryptUpdate(s.ctx.get(), s.buf.data(), &n, s.zeros.data(),
                              static_cast<int>(s.zeros.size())) != 1 ||
            n != static_cast<int>(s.buf.size()))
            throw std::runtime_error("ChaCha20 keystream failed");
        s.pos = 0;
    }
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | s.buf[s.pos + static_cast<size_t>(i)];
    s.pos += 8;
    return v;
}

uint64_t Prng::below(uint64_t bound) {
    const uint64_t threshold = (0 - bound) % bound;  // 2^64 mod bound
    for (;;) {
        const uint64_t r = next64();
        if (r >= threshold) return r % bound;
    }
}

}  // namespace stego
