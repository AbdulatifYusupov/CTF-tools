#include "crypto/Crypto.hpp"
#include "stego/AdaptiveLsb.hpp"
#include "stego/Pipeline.hpp"

#include <opencv2/core.hpp>

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

int main() {
    const auto kp    = crypto::generateKeyPair();
    const auto other = crypto::generateKeyPair();

    crypto::Bytes payload(1000);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i * 31 + 7);

    for (int k : {1, 2}) {
        cv::Mat img(256, 256, CV_8UC3);
        cv::RNG rng(12345);
        rng.fill(img, cv::RNG::UNIFORM, 0, 256);  // noise -> almost every pixel is "busy"
        const cv::Mat original = img.clone();

        stego::Carrier carrier = img;  // shares pixel data with img
        const stego::AdaptiveLsb algo(stego::AdaptiveLsb::Options{k, 32});

        stego::embedMessage(algo, carrier, payload, kp.publicKey);

        // Only the low k bits may change
        const auto& res = std::get<cv::Mat>(carrier);
        cv::Mat diff;
        cv::absdiff(original, res, diff);
        double mx = 0;
        cv::minMaxLoc(diff.reshape(1), nullptr, &mx);
        CHECK(mx > 0);
        CHECK(mx <= (1 << k) - 1);

        // Roundtrip
        CHECK(stego::extractMessage(algo, carrier, kp.privateKey) == payload);

        // Wrong key must throw, never return garbage
        bool threw = false;
        try { (void)stego::extractMessage(algo, carrier, other.privateKey); }
        catch (const crypto::Error&) { threw = true; }
        CHECK(threw);

        // Too-small carrier must throw length_error
        stego::Carrier tiny = cv::Mat(16, 16, CV_8UC3, cv::Scalar::all(0));
        threw = false;
        try { stego::embedMessage(algo, tiny, payload, kp.publicKey); }
        catch (const std::length_error&) { threw = true; }
        CHECK(threw);

        std::printf("k=%d: OK (max pixel change %.0f)\n", k, mx);
    }
    // Password mode on the same algorithm
    {
        cv::Mat img(256, 256, CV_8UC3);
        cv::RNG(12345).fill(img, cv::RNG::UNIFORM, 0, 256);
        stego::Carrier carrier = img.clone();
        const stego::AdaptiveLsb algo(stego::AdaptiveLsb::Options{1, 32});
        stego::embedMessageWithPassword(algo, carrier, payload, "correct horse battery staple");
        CHECK(stego::extractMessageWithPassword(algo, carrier, "correct horse battery staple") == payload);
        bool threw = false;
        try { (void)stego::extractMessageWithPassword(algo, carrier, "wrong password"); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        std::puts("password mode: OK");
    }

    std::puts("adaptive_lsb_roundtrip: OK");
    return 0;
}
