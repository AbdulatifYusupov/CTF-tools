#include "StegoWorker.hpp"

#include "crypto/Crypto.hpp"
#include "stego/Pipeline.hpp"

#include <QPromise>
#include <QtConcurrent>

#include <span>
#include <string_view>

namespace app {
namespace {

using EmbedOutcome   = detail::Outcome<EmbedReport>;
using ExtractOutcome = detail::Outcome<ExtractReport>;
using AlgoPtr        = StegoWorker::AlgoPtr;

// Which envelope to use and its secret: recipient public/private key (DER) or the password (UTF-8).
struct Envelope {
    bool password = false;
    QByteArray secret;
};

std::span<const uint8_t> bytesOf(const QByteArray& b) {
    return {reinterpret_cast<const uint8_t*>(b.constData()), static_cast<size_t>(b.size())};
}

template <class T>
void throwIfCanceled(QPromise<T>& p) {
    if (p.isCanceled()) throw stego::OperationCancelled();
}

// Runs on the pool thread. Never lets an exception escape: errors travel in the Outcome,
// cancellation is signalled by the future's canceled flag (no value, no error).
void runEmbed(QPromise<EmbedOutcome>& promise, AlgoPtr algo, cv::Mat cover, QByteArray payload,
              Envelope env) {
    promise.setProgressRange(0, 100);
    EmbedOutcome out;
    try {
        EmbedReport rep;
        stego::Carrier carrier = cover.clone();  // embed() works in place

        const size_t header = env.password ? stego::kPasswordHeaderBytes : stego::kKemHeaderBytes;
        rep.capacityBits    = algo->capacityBits(carrier, header);
        rep.usedBits = (header + crypto::kLengthFieldSize +
                        static_cast<size_t>(payload.size()) + crypto::kTagSize) * 8;
        throwIfCanceled(promise);

        // 0..70 %: embedding
        const stego::Progress prog = [&promise](float f) {
            promise.setProgressValue(static_cast<int>(f * 70.f));
            return !promise.isCanceled();
        };
        if (env.password)
            stego::embedMessageWithPassword(
                *algo, carrier, bytesOf(payload),
                std::string_view(env.secret.constData(), static_cast<size_t>(env.secret.size())), prog);
        else
            stego::embedMessage(*algo, carrier, bytesOf(payload), bytesOf(env.secret), prog);
        rep.stego = std::get<cv::Mat>(carrier);

        // 70..100 %: metrics, checked for cancellation between steps (SSIM is the slow one)
        throwIfCanceled(promise);
        promise.setProgressValue(75);
        rep.psnr = analysis::psnr(cover, rep.stego);
        throwIfCanceled(promise);
        promise.setProgressValue(80);
        rep.ssim = analysis::ssim(cover, rep.stego);
        throwIfCanceled(promise);
        promise.setProgressValue(95);
        rep.chiBefore = analysis::chiSquare(cover);
        rep.chiAfter  = analysis::chiSquare(rep.stego);
        throwIfCanceled(promise);
        promise.setProgressValue(97);
        rep.lsbBefore = analysis::lsbDetection(cover);
        rep.lsbAfter  = analysis::lsbDetection(rep.stego);
        promise.setProgressValue(100);

        out.value = std::move(rep);
    } catch (const stego::OperationCancelled&) {
        // canceled flag is already set on the promise
    } catch (const std::exception& e) {
        out.error = QString::fromUtf8(e.what());
    }
    promise.addResult(std::move(out));
}

void runExtract(QPromise<ExtractOutcome>& promise, AlgoPtr algo, cv::Mat stegoImage,
                Envelope env) {
    promise.setProgressRange(0, 100);
    ExtractOutcome out;
    try {
        const stego::Carrier carrier = stegoImage;  // read-only use
        const stego::Progress prog   = [&promise](float f) {
            promise.setProgressValue(static_cast<int>(f * 100.f));
            return !promise.isCanceled();
        };
        const auto data =
            env.password
                ? stego::extractMessageWithPassword(
                      *algo, carrier,
                      std::string_view(env.secret.constData(), static_cast<size_t>(env.secret.size())), prog)
                : stego::extractMessage(*algo, carrier, bytesOf(env.secret), prog);

        ExtractReport rep;
        rep.payload = QByteArray(reinterpret_cast<const char*>(data.data()),
                                 static_cast<qsizetype>(data.size()));
        out.value = std::move(rep);
    } catch (const stego::OperationCancelled&) {
    } catch (const std::exception& e) {
        out.error = QString::fromUtf8(e.what());
    }
    promise.addResult(std::move(out));
}

}  // namespace

StegoWorker::StegoWorker(QObject* parent) : QObject(parent) {
    pool_.setMaxThreadCount(1);  // one operation at a time; also keeps memory use predictable

    connect(&embedWatcher_, &QFutureWatcher<EmbedOutcome>::progressValueChanged, this,
            &StegoWorker::progress);
    connect(&extractWatcher_, &QFutureWatcher<ExtractOutcome>::progressValueChanged, this,
            &StegoWorker::progress);

    connect(&embedWatcher_, &QFutureWatcher<EmbedOutcome>::finished, this, [this] {
        const auto f = embedWatcher_.future();
        if (f.isCanceled()) return emit cancelled();
        if (f.resultCount() == 0) return emit failed(tr("Operation produced no result"));
        const EmbedOutcome r = f.result();
        if (!r.error.isEmpty()) return emit failed(r.error);
        if (!r.value) return emit failed(tr("Operation produced no result"));
        emit embedFinished(*r.value);
    });

    connect(&extractWatcher_, &QFutureWatcher<ExtractOutcome>::finished, this, [this] {
        const auto f = extractWatcher_.future();
        if (f.isCanceled()) return emit cancelled();
        if (f.resultCount() == 0) return emit failed(tr("Operation produced no result"));
        const ExtractOutcome r = f.result();
        if (!r.error.isEmpty()) return emit failed(r.error);
        if (!r.value) return emit failed(tr("Operation produced no result"));
        emit extractFinished(*r.value);
    });
}

StegoWorker::~StegoWorker() {
    cancel();
    pool_.waitForDone();
}

bool StegoWorker::busy() const { return embedWatcher_.isRunning() || extractWatcher_.isRunning(); }

void StegoWorker::embed(AlgoPtr algo, const cv::Mat& cover, QByteArray payload,
                        QByteArray recipientPublicKeyDer) {
    if (busy()) return emit failed(tr("Another operation is still running"));
    if (!algo || cover.empty()) return emit failed(tr("No algorithm or cover image"));
    embedWatcher_.setFuture(QtConcurrent::run(&pool_, runEmbed, std::move(algo), cover.clone(),
                                              std::move(payload),
                                              Envelope{false, std::move(recipientPublicKeyDer)}));
}

void StegoWorker::embedWithPassword(AlgoPtr algo, const cv::Mat& cover, QByteArray payload,
                                    QByteArray passwordUtf8) {
    if (busy()) return emit failed(tr("Another operation is still running"));
    if (!algo || cover.empty()) return emit failed(tr("No algorithm or cover image"));
    if (passwordUtf8.isEmpty()) return emit failed(tr("Password is empty"));
    embedWatcher_.setFuture(QtConcurrent::run(&pool_, runEmbed, std::move(algo), cover.clone(),
                                              std::move(payload),
                                              Envelope{true, std::move(passwordUtf8)}));
}

void StegoWorker::extract(AlgoPtr algo, const cv::Mat& stegoImage,
                          QByteArray recipientPrivateKeyDer) {
    if (busy()) return emit failed(tr("Another operation is still running"));
    if (!algo || stegoImage.empty()) return emit failed(tr("No algorithm or image"));
    extractWatcher_.setFuture(QtConcurrent::run(&pool_, runExtract, std::move(algo),
                                                stegoImage.clone(),
                                                Envelope{false, std::move(recipientPrivateKeyDer)}));
}

void StegoWorker::extractWithPassword(AlgoPtr algo, const cv::Mat& stegoImage,
                                      QByteArray passwordUtf8) {
    if (busy()) return emit failed(tr("Another operation is still running"));
    if (!algo || stegoImage.empty()) return emit failed(tr("No algorithm or image"));
    if (passwordUtf8.isEmpty()) return emit failed(tr("Password is empty"));
    extractWatcher_.setFuture(QtConcurrent::run(&pool_, runExtract, std::move(algo),
                                                stegoImage.clone(),
                                                Envelope{true, std::move(passwordUtf8)}));
}

void StegoWorker::cancel() {
    embedWatcher_.cancel();
    extractWatcher_.cancel();
}

}  // namespace app
