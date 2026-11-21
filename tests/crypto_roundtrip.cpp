#include "crypto/Crypto.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

int main() {
    using namespace crypto;

    const auto kp = generateKeyPair();
    const std::string msg = "hello, stego";
    const Bytes payload(msg.begin(), msg.end());

    // Sender
    Bytes kemCt;
    const auto tx   = Session::encapsulate(kp.publicKey, kemCt);
    const auto body = tx.sealBody(payload);
    CHECK(kemCt.size() == kKemCiphertextSize);
    CHECK(body.size() == kLengthFieldSize + payload.size() + kTagSize);

    // Receiver: follows the same two-step read as Pipeline::extractMessage
    const auto rx = Session::decapsulate(kp.privateKey, kemCt);
    CHECK(rx.bodySeed() == tx.bodySeed());
    const size_t total = rx.bodySizeFromLengthField({body.data(), kLengthFieldSize});
    CHECK(total == body.size());
    CHECK(rx.openBody(body) == payload);

    // Tampering must be detected
    auto bad = body;
    bad[kLengthFieldSize] ^= 1;
    bool threw = false;
    try { (void)rx.openBody(bad); } catch (const Error&) { threw = true; }
    CHECK(threw);

    // Wrong key: ML-KEM implicit rejection -> must fail at length or tag, never succeed
    const auto other = generateKeyPair();
    const auto wrong = Session::decapsulate(other.privateKey, kemCt);
    threw = false;
    try { (void)wrong.openBody(body); } catch (const Error&) { threw = true; }
    CHECK(threw);

    // ---- password mode (Argon2id) ----------------------------------------------------------------
    {
        const ArgonCost fast{1024, 1};  // 1 MiB, 1 pass: quick enough for tests
        const Bytes salt = randomBytes(kSaltSize);
        CHECK(salt.size() == kSaltSize && salt != randomBytes(kSaltSize));  // CSPRNG: fresh every call

        const auto a1 = Session::fromPassword("correct horse", salt, fast);
        const auto a2 = Session::fromPassword("correct horse", salt, fast);
        CHECK(a1.bodySeed() == a2.bodySeed());  // deterministic: sender and receiver agree
        const auto sealed = a1.sealBody(payload);
        CHECK(a2.openBody(sealed) == payload);

        // wrong password, different salt, different cost: each must derive a different key
        auto fails = [&](const Session& s) {
            try { (void)s.openBody(sealed); } catch (const Error&) { return true; }
            return false;
        };
        CHECK(fails(Session::fromPassword("correct horsf", salt, fast)));
        Bytes salt2 = salt;
        salt2[0] ^= 1;
        CHECK(fails(Session::fromPassword("correct horse", salt2, fast)));
        CHECK(fails(Session::fromPassword("correct horse", salt, ArgonCost{1024, 2})));

        // argument validation
        auto rejects = [&](auto&& f) {
            try { f(); } catch (const Error&) { return true; }
            return false;
        };
        CHECK(rejects([&] { (void)Session::fromPassword("", salt, fast); }));
        CHECK(rejects([&] { (void)Session::fromPassword("x", Bytes(8), fast); }));

        // the default cost (64 MiB, 3 passes) must be accepted by this OpenSSL build
        const auto d = Session::fromPassword("correct horse", salt);
        CHECK(d.bodySeed() != a1.bodySeed());
    }

    std::puts("crypto_roundtrip: OK");
    return 0;
}
