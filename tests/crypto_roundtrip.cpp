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

    std::puts("crypto_roundtrip: OK");
    return 0;
}
