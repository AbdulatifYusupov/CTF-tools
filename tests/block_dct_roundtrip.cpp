#include "analysis/Metrics.hpp"
#include "crypto/Crypto.hpp"
#include "stego/BlockDct.hpp"
#include "stego/Pipeline.hpp"

#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

// Smooth pattern + noise + flat saturated patches (clipping is the hard case for DCT).
static cv::Mat makeImage(int w, int h) {
    cv::Mat m(h, w, CV_8UC3);
    cv::RNG rng(99);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                double v = 128 + 70 * std::sin(x / 37.0 + c) * std::cos(y / 53.0 - c) + rng.gaussian(6.0);
                if ((x / 64 + y / 64) % 5 == 0) v = 255;
                else if ((x / 64 + y / 64) % 7 == 3) v = 0;
                m.at<cv::Vec3b>(y, x)[c] = cv::saturate_cast<uint8_t>(v);
            }
    return m;
}

int main() {
    const auto kp    = crypto::generateKeyPair();
    const auto other = crypto::generateKeyPair();
    crypto::Bytes payload(800);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i * 13 + 5);

    const cv::Mat cover = makeImage(1024, 768);
    stego::Carrier carrier = cover.clone();
    const stego::BlockDct algo;  // defaults: pairs=2, strength=8, parity=32

    CHECK(algo.capacityBits(carrier, stego::kKemHeaderBytes) >=
          (stego::kKemHeaderBytes + 4 + 16 + payload.size()) * 8);
    stego::embedMessage(algo, carrier, payload, kp.publicKey);

    const cv::Mat& res = std::get<cv::Mat>(carrier);
    const double psnr = analysis::psnr(cover, res);
    const double ssim = analysis::ssim(cover, res);
    std::printf("PSNR %.2f dB, SSIM %.4f\n", psnr, ssim);
    // Guard bands, not targets. Measured with an independent SSIM implementation on this image
    // (~70% fill, ~1/3 flat saturated patches): PSNR 44.3 dB, SSIM 0.973 at strength 12. The default is
    // now 8 (chosen from real-photo runs); on this image it is expected to be no worse, not re-measured.
    // Flat patches are the weak spot (SSIM there ~0.95), noisy texture is much better.
    // strength 24 would give PSNR 38.2 / SSIM 0.915 on the same image.
    CHECK(psnr > 40.0 && ssim > 0.95);

    CHECK(stego::extractMessage(algo, carrier, kp.privateKey) == payload);

    // Wrong key: the header still decodes, but ML-KEM implicit rejection yields a random secret,
    // hence a random body seed. With BlockDct the Reed-Solomon decode of the length field then
    // fails (std::runtime_error from the algorithm); with the LSB algorithms the length/tag check
    // fails instead (crypto::Error, which is itself a std::runtime_error). Either way it must
    // throw and never return data, so catching the common base is the right contract.
    bool threw = false;
    try { (void)stego::extractMessage(algo, carrier, other.privateKey); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    // Lossless file format roundtrip: this is the "save as PNG" path
    std::vector<uint8_t> png;
    CHECK(cv::imencode(".png", res, png));
    const stego::Carrier reloaded = cv::imdecode(png, cv::IMREAD_COLOR);
    CHECK(stego::extractMessage(algo, reloaded, kp.privateKey) == payload);

    // Local damage (a 40x40 patch overwritten with noise) must be absorbed by Reed-Solomon
    cv::Mat damaged = res.clone();
    cv::RNG rng(5);
    cv::Mat noise(40, 40, CV_8UC3);
    rng.fill(noise, cv::RNG::UNIFORM, 0, 256);
    noise.copyTo(damaged(cv::Rect(300, 200, 40, 40)));
    CHECK(stego::extractMessage(algo, stego::Carrier{damaged}, kp.privateKey) == payload);

    // A clean cover has no data: must fail loudly, never return something
    threw = false;
    try { (void)stego::extractMessage(algo, stego::Carrier{cover}, kp.privateKey); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Too much data must be rejected up front
    stego::Carrier small = makeImage(256, 256);
    threw = false;
    try { stego::embedMessage(algo, small, payload, kp.publicKey); }
    catch (const std::length_error&) { threw = true; }
    CHECK(threw);

    // ---- password mode: a 16-byte header instead of 1088 makes small images usable ---------------
    {
        const std::string pw = "correct horse battery staple";

        // same big image: clearly more room than KEM mode (the header cost ~10 kbit less)
        CHECK(stego::maxPayloadBytes(algo, carrier, stego::kPasswordHeaderBytes) >
              stego::maxPayloadBytes(algo, carrier, stego::kKemHeaderBytes) + 900);

        stego::Carrier pc = cover.clone();
        stego::embedMessageWithPassword(algo, pc, payload, pw);
        CHECK(stego::extractMessageWithPassword(algo, pc, pw) == payload);

        threw = false;
        try { (void)stego::extractMessageWithPassword(algo, pc, "wrong password"); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);

        // a file made in one mode must not be readable as the other
        threw = false;
        try { (void)stego::extractMessage(algo, pc, kp.privateKey); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);

        // 256x256 photo-like image: KEM mode cannot fit anything, password mode carries 100 bytes
        cv::Mat t(256, 256, CV_8UC3);
        cv::RNG r(8);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                for (int c = 0; c < 3; ++c)
                    t.at<cv::Vec3b>(y, x)[c] = cv::saturate_cast<uint8_t>(
                        128 + 60 * std::sin(x / 29.0 + c) * std::cos(y / 41.0) + r.gaussian(5.0));
        stego::Carrier tiny = t;
        const crypto::Bytes small100(payload.begin(), payload.begin() + 100);
        CHECK(stego::maxPayloadBytes(algo, tiny, stego::kKemHeaderBytes) == 0);
        CHECK(stego::maxPayloadBytes(algo, tiny, stego::kPasswordHeaderBytes) >= 100);
        stego::embedMessageWithPassword(algo, tiny, small100, pw);
        CHECK(stego::extractMessageWithPassword(algo, tiny, pw) == small100);
    }

    std::puts("block_dct_roundtrip: OK");
    return 0;
}
