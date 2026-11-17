#include "StegoWorker.hpp"

#include "crypto/Crypto.hpp"
#include "stego/Pipeline.hpp"

#include <QPromise>
#include <QtConcurrent>

#include <span>

namespace app {
namespace {

using EmbedOutcome   = detail::Outcome<EmbedReport>;
using ExtractOutcome = detail::Outcome<ExtractReport>;
using AlgoPtr        = StegoWorker::AlgoPtr;

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
              QByteArray pubKey) {
    promise.setProgressRange(0, 100);
    EmbedOutcome out;
    try {
        EmbedReport rep;
        stego::Carrier carrier = cover.clone();  // embed() works in place

        rep.capacityBits = algo->capacityBits(carrier);
        rep.usedBits = (stego::kHeaderBytes + crypto::kLengthFieldSize +
                        static_cast<size_t>(payload.size()) + crypto::kTagSize) * 8;
        throwIfCanceled(promise);

        // 0..70 %: embedding
        const stego::Progress prog = [&promise](float f) {
            promise.setProgressValue(static_cast<int>(f * 70.f));
            return !promise.isCanceled();
        };
        stego::embedMessage(*algo, carrier, bytesOf(payload), bytesOf(pubKey), prog);
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
                QByteArray privKey) {
    promise.setProgressRange(0, 100);
    ExtractOutcome out;
    try {
        const stego::Carrier carrier = stegoImage;  // read-only use
        const stego::Progress prog   = [&promise](float f) {
            promise.setProgressValue(static_cast<int>(f * 100.f));
            return !promise.isCanceled();
        };
        const auto data = stego::extractMessage(*algo, carrier, bytesOf(privKey), prog);

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
                                              std::move(recipientPublicKeyDer)));
}

void StegoWorker::extract(AlgoPtr algo, const cv::Mat& stegoImage,
                          QByteArray recipientPrivateKeyDer) {
    if (busy()) return emit failed(tr("Another operation is still running"));
    if (!algo || stegoImage.empty()) return emit failed(tr("No algorithm or image"));
    extractWatcher_.setFuture(QtConcurrent::run(&pool_, runExtract, std::move(algo),
                                                stegoImage.clone(),
                                                std::move(recipientPrivateKeyDer)));
}

void StegoWorker::cancel() {
    embedWatcher_.cancel();
    extractWatcher_.cancel();
}

}  // namespace app
