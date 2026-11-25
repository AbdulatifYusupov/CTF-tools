#include "StegoWorker.hpp"

#include "crypto/Crypto.hpp"
#include "stego/AdaptiveLsb.hpp"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

// Headless end-to-end test of the worker: embed -> extract -> compare, then cancel.
int main(int argc, char** argv) {
    QCoreApplication qapp(argc, argv);

    const auto kp = crypto::generateKeyPair();
    const QByteArray pub(reinterpret_cast<const char*>(kp.publicKey.data()),
                         static_cast<qsizetype>(kp.publicKey.size()));
    const QByteArray priv(reinterpret_cast<const char*>(kp.privateKey.data()),
                          static_cast<qsizetype>(kp.privateKey.size()));

    cv::Mat cover(256, 256, CV_8UC3);
    cv::RNG(7).fill(cover, cv::RNG::UNIFORM, 0, 256);
    const QByteArray payload = "secret message from the worker test";

    const auto algo = std::make_shared<const stego::AdaptiveLsb>(
        stego::AdaptiveLsb::Options{1, 32});

    app::StegoWorker worker;
    int exitCode = 1;
    int lastProgress = -1;
    bool monotone = true, sawProgress = false;
    enum class Phase { Embed, Extract, PwEmbed, PwExtract, Cancel } phase = Phase::Embed;
    const QByteArray password = "correct horse battery staple";

    auto fail = [&](const char* msg) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        exitCode = 1;
        qapp.quit();
    };

    QObject::connect(&worker, &app::StegoWorker::progress, [&](int p) {
        sawProgress = true;
        if (p < lastProgress) monotone = false;
        lastProgress = p;
    });
    QObject::connect(&worker, &app::StegoWorker::failed,
                     [&](const QString& m) { fail(qPrintable(m)); });

    QObject::connect(&worker, &app::StegoWorker::embedFinished, [&](const app::EmbedReport& r) {
        if (phase == Phase::PwEmbed) {
            if (r.stego.empty() || r.usedBits == 0 || r.usedBits > r.capacityBits) return fail("bad password embed report");
            if (r.lsbBefore.size() != 3 || r.lsbAfter.size() != 3) return fail("missing LSB detection results");
            std::printf("password embed OK: psnr=%.2f used=%zu/%zu bits\n", r.psnr, r.usedBits, r.capacityBits);
            phase = Phase::PwExtract;
            worker.extractWithPassword(algo, r.stego, password);
            return;
        }
        if (phase != Phase::Embed) return fail("unexpected embedFinished");
        if (!monotone) return fail("progress went backwards");
        if (!sawProgress) return fail("no progress reported");
        if (r.stego.empty() || r.psnr < 40.0 || r.ssim < 0.9) return fail("bad metrics");
        if (r.usedBits == 0 || r.usedBits > r.capacityBits) return fail("bad capacity numbers");
        std::printf("embed OK: psnr=%.2f ssim=%.5f used=%zu/%zu bits\n", r.psnr, r.ssim,
                    r.usedBits, r.capacityBits);
        phase = Phase::Extract;
        lastProgress = -1;
        worker.extract(algo, r.stego, priv);
    });

    QObject::connect(&worker, &app::StegoWorker::extractFinished,
                     [&](const app::ExtractReport& r) {
        if (phase != Phase::Extract && phase != Phase::PwExtract) return fail("unexpected extractFinished");
        if (phase == Phase::Extract && r.payload != payload) return fail("payload mismatch");
        if (phase == Phase::Extract) {
            std::puts("extract OK");
            phase = Phase::PwEmbed;
            worker.embedWithPassword(algo, cover, payload, password);
            return;
        }
        if (phase == Phase::PwExtract) {
            if (r.payload != payload) return fail("password payload mismatch");
            std::puts("password extract OK");
            phase = Phase::Cancel;
            worker.embed(algo, cover, payload, pub);
            worker.cancel();  // must end in cancelled(), never in embedFinished/failed
            return;
        }
        fail("unexpected extractFinished");
    });

    QObject::connect(&worker, &app::StegoWorker::cancelled, [&] {
        if (phase != Phase::Cancel) return fail("unexpected cancelled");
        std::puts("cancel OK");
        exitCode = 0;
        qapp.quit();
    });

    worker.embed(algo, cover, payload, pub);
    QTimer::singleShot(60000, [&] { fail("timeout"); });

    qapp.exec();
    std::puts(exitCode == 0 ? "stego_worker_test: OK" : "stego_worker_test: FAILED");
    return exitCode;
}
