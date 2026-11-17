#pragma once
#include "analysis/Metrics.hpp"
#include "stego/IStegoAlgorithm.hpp"

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <opencv2/core.hpp>

#include <memory>
#include <optional>

namespace app {

struct EmbedReport {
    cv::Mat stego;  // independent copy, never shares pixels with the cover
    double psnr = 0;
    double ssim = 0;
    analysis::ChiSquareResult chiBefore;
    analysis::ChiSquareResult chiAfter;
    size_t capacityBits = 0;  // of the cover, for this algorithm and options
    size_t usedBits     = 0;  // header + length + ciphertext + tag
};

struct ExtractReport {
    QByteArray payload;
};

namespace detail {
template <class T>
struct Outcome {
    std::optional<T> value;  // empty on error or cancel
    QString error;           // empty on success or cancel
};
}  // namespace detail

// Runs embed / extract (+ metrics) on a private single-thread pool and reports back
// through signals on the GUI thread. One operation at a time.
//
//   worker.embed(algo, cover, payload, pubKey);
//   -> progress(0..100) ... -> embedFinished(report) | failed(msg) | cancelled()
class StegoWorker : public QObject {
    Q_OBJECT
public:
    using AlgoPtr = std::shared_ptr<const stego::IStegoAlgorithm>;

    explicit StegoWorker(QObject* parent = nullptr);
    ~StegoWorker() override;

    bool busy() const;

    // `cover` is copied, the caller keeps ownership of its pixels.
    void embed(AlgoPtr algo, const cv::Mat& cover, QByteArray payload,
               QByteArray recipientPublicKeyDer);
    void extract(AlgoPtr algo, const cv::Mat& stegoImage, QByteArray recipientPrivateKeyDer);

public slots:
    void cancel();

signals:
    void progress(int percent);
    void embedFinished(const app::EmbedReport& report);
    void extractFinished(const app::ExtractReport& report);
    void failed(const QString& message);
    void cancelled();

private:
    using EmbedOutcome   = detail::Outcome<EmbedReport>;
    using ExtractOutcome = detail::Outcome<ExtractReport>;

    QThreadPool pool_;  // declared first -> destroyed last
    QFutureWatcher<EmbedOutcome> embedWatcher_;
    QFutureWatcher<ExtractOutcome> extractWatcher_;
};

}  // namespace app
