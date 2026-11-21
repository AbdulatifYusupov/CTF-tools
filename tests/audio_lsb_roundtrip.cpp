#include "crypto/Crypto.hpp"
#include "stego/AudioIO.hpp"
#include "stego/AudioLsb.hpp"
#include "stego/Pipeline.hpp"

#include <sndfile.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

static stego::AudioData synth() {
    std::mt19937 rng(42);
    stego::AudioData a;
    a.channels = 2;
    a.sampleRate = 44100;
    a.sfFormat = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    const size_t frames = 44100 * 2;
    a.samples.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c) {
            const double t = static_cast<double>(i) / 44100.0;
            a.samples[i * 2 + c] = static_cast<int16_t>(
                8000 * std::sin(2 * M_PI * 440 * t * (c + 1)) + static_cast<int>(rng() % 200) - 100);
        }
    for (size_t i = 30000; i < 52000; ++i) a.samples[i * 2] = a.samples[i * 2 + 1] = 0;  // silence
    return a;
}

int main() {
    const auto kp    = crypto::generateKeyPair();
    const auto other = crypto::generateKeyPair();
    crypto::Bytes payload(1000);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i * 17 + 3);

    for (int k : {1, 2}) {
        const auto cover = synth();
        stego::Carrier carrier = cover;
        const stego::AudioLsb algo(stego::AudioLsb::Options{k, 64});

        stego::embedMessage(algo, carrier, payload, kp.publicKey);

        const auto& res = std::get<stego::AudioData>(carrier);
        int maxd = 0;
        for (size_t i = 0; i < res.samples.size(); ++i)
            maxd = std::max(maxd, std::abs(res.samples[i] - cover.samples[i]));
        CHECK(maxd > 0 && maxd <= (1 << k) - 1);
        for (size_t i = 30000; i < 52000; ++i) CHECK(res.samples[i * 2] == 0 && res.samples[i * 2 + 1] == 0);

        CHECK(stego::extractMessage(algo, carrier, kp.privateKey) == payload);

        bool threw = false;
        try { (void)stego::extractMessage(algo, carrier, other.privateKey); }
        catch (const crypto::Error&) { threw = true; }
        CHECK(threw);

        // File roundtrip: what we write must read back sample-for-sample and still decode
        const auto path = (std::filesystem::temp_directory_path() / "stego_audio_test.wav").string();
        stego::writeAudio(path, res);
        const auto back = stego::readAudio(path);
        CHECK(back.channels == res.channels && back.sampleRate == res.sampleRate);
        CHECK(back.samples == res.samples);
        CHECK(stego::extractMessage(algo, stego::Carrier{back}, kp.privateKey) == payload);
        std::filesystem::remove(path);

        std::printf("k=%d: OK (max sample change %d)\n", k, maxd);
    }

    // Password mode on the same algorithm
    {
        stego::Carrier carrier = synth();
        const stego::AudioLsb algo(stego::AudioLsb::Options{1, 64});
        stego::embedMessageWithPassword(algo, carrier, payload, "correct horse battery staple");
        CHECK(stego::extractMessageWithPassword(algo, carrier, "correct horse battery staple") == payload);
        bool threw = false;
        try { (void)stego::extractMessageWithPassword(algo, carrier, "wrong password"); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        std::puts("password mode: OK");
    }

    // FLAC is optional in libsndfile builds: skip quietly if unavailable
    try {
        auto a = synth();
        a.sfFormat = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
        const auto path = (std::filesystem::temp_directory_path() / "stego_audio_test.flac").string();
        stego::writeAudio(path, a);
        CHECK(stego::readAudio(path).samples == a.samples);
        std::filesystem::remove(path);
        std::puts("flac roundtrip OK");
    } catch (const std::runtime_error& e) {
        std::printf("flac skipped: %s\n", e.what());
    }

    std::puts("audio_lsb_roundtrip: OK");
    return 0;
}
