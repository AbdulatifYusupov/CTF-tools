#include "MainWindow.hpp"

#include "AlgoFactory.hpp"
#include "CompareWidget.hpp"
#include "ImageConvert.hpp"

#include "analysis/Metrics.hpp"
#include "crypto/Crypto.hpp"
#include "stego/Pipeline.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <exception>

namespace app {
namespace {

const char* const kImageFilter = "Images (*.png *.bmp *.tif *.tiff *.jpg *.jpeg);;All files (*)";

QByteArray readAll(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return {};
    }
    return f.readAll();
}

bool writeAll(const QString& path, const QByteArray& data, QString* error, bool ownerOnly = false) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (ownerOnly) f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (f.write(data) != data.size()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QString lsbText(const std::vector<analysis::LsbDetection>& d, bool rs) {
    double sum = 0;
    int n = 0;
    bool flagged = false;
    for (const auto& c : d) {
        const analysis::LsbEstimate& e = rs ? c.rs : c.spa;
        if (!e.valid) continue;
        sum += e.p;
        ++n;
        flagged = flagged || e.lowReliability || e.approx;
    }
    if (n == 0) return QStringLiteral("n/a");
    return QString::number(sum / n, 'f', 3) + (flagged ? QStringLiteral(" !") : QString());
}

bool looksLikeText(const QByteArray& b) {
    if (b.contains('\0')) return false;
    const QString s = QString::fromUtf8(b);
    return !s.contains(QChar::ReplacementCharacter) && s.toUtf8() == b;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Stego Tool"));
    resize(1280, 800);
    buildUi();

    connect(&worker_, &StegoWorker::progress, this, &MainWindow::onProgress);
    connect(&worker_, &StegoWorker::embedFinished, this, &MainWindow::onEmbedFinished);
    connect(&worker_, &StegoWorker::extractFinished, this, &MainWindow::onExtractFinished);
    connect(&worker_, &StegoWorker::failed, this, &MainWindow::onFailed);
    connect(&worker_, &StegoWorker::cancelled, this, &MainWindow::onCancelled);

    onAlgoOrModeChanged();
    updateButtons();
}

// ---- UI construction ---------------------------------------------------------------------------

void MainWindow::buildUi() {
    auto* split = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(split);

    // ---------- left: controls ----------
    auto* controls = new QWidget;
    auto* cl       = new QVBoxLayout(controls);

    // cover
    {
        auto* box = new QGroupBox(tr("Cover image"));
        auto* l   = new QVBoxLayout(box);
        auto* row = new QHBoxLayout;
        openCoverBtn_ = new QPushButton(tr("Open cover…"));
        openStegoBtn_ = new QPushButton(tr("Open stego image…"));
        row->addWidget(openCoverBtn_);
        row->addWidget(openStegoBtn_);
        l->addLayout(row);
        coverLabel_    = new QLabel(tr("No image"));
        capacityLabel_ = new QLabel;
        capacityLabel_->setWordWrap(true);
        l->addWidget(coverLabel_);
        l->addWidget(capacityLabel_);
        cl->addWidget(box);
        connect(openCoverBtn_, &QPushButton::clicked, this, &MainWindow::openCover);
        connect(openStegoBtn_, &QPushButton::clicked, this, &MainWindow::openStego);
    }

    // payload
    {
        auto* box = new QGroupBox(tr("Message to hide"));
        auto* l   = new QVBoxLayout(box);
        payloadEdit_ = new QPlainTextEdit;
        payloadEdit_->setPlaceholderText(tr("Type text here, or use a file…"));
        payloadEdit_->setMaximumHeight(110);
        auto* row = new QHBoxLayout;
        auto* fileBtn = new QPushButton(tr("Use file…"));
        clearFileBtn_ = new QPushButton(tr("Clear file"));
        payloadFileLabel_ = new QLabel;
        row->addWidget(fileBtn);
        row->addWidget(clearFileBtn_);
        row->addWidget(payloadFileLabel_, 1);
        l->addWidget(payloadEdit_);
        l->addLayout(row);
        cl->addWidget(box);
        connect(fileBtn, &QPushButton::clicked, this, &MainWindow::choosePayloadFile);
        connect(clearFileBtn_, &QPushButton::clicked, this, &MainWindow::clearPayloadFile);
        connect(payloadEdit_, &QPlainTextEdit::textChanged, this, &MainWindow::updateButtons);
    }

    // algorithm
    {
        algoBox_   = new QGroupBox(tr("Algorithm"));
        auto* form = new QFormLayout(algoBox_);
        algoCombo_ = new QComboBox;
        algoCombo_->addItem(tr("Adaptive LSB"), QStringLiteral("adaptive-lsb"));
        algoCombo_->addItem(tr("Block DCT (Koch-Zhao, with Reed-Solomon)"), QStringLiteral("block-dct"));
        auto spin = [](int lo, int hi, int v) {
            auto* s = new QSpinBox;
            s->setRange(lo, hi);
            s->setValue(v);
            return s;
        };
        const AlgoSettings d;
        bitsSpin_      = spin(1, 2, d.bits);
        thresholdSpin_ = spin(0, 4096, d.threshold);
        pairsSpin_     = spin(1, 3, d.pairs);
        paritySpin_    = spin(2, 100, d.parity);
        strengthSpin_  = spin(8, 100, d.strength);
        bitsSpin_->setToolTip(tr("LSBs used per colour channel. Must match at extraction."));
        thresholdSpin_->setToolTip(tr("Edge-strength threshold of the complexity map. Must match at extraction."));
        pairsSpin_->setToolTip(tr("Coefficient pairs (bits) per 8x8 block. Must match at extraction."));
        paritySpin_->setToolTip(tr("Reed-Solomon parity bytes per block. Must match at extraction."));
        strengthSpin_->setToolTip(tr("Embedding strength. Only the embedder needs it; it is not stored in profiles."));
        form->addRow(tr("Algorithm"), algoCombo_);
        form->addRow(tr("Bits per channel"), bitsSpin_);
        form->addRow(tr("Threshold"), thresholdSpin_);
        form->addRow(tr("Pairs per block"), pairsSpin_);
        form->addRow(tr("RS parity symbols"), paritySpin_);
        form->addRow(tr("Strength (embed only)"), strengthSpin_);
        cl->addWidget(algoBox_);

        connect(algoCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onAlgoOrModeChanged);
        for (QSpinBox* s : {bitsSpin_, thresholdSpin_, pairsSpin_, paritySpin_})
            connect(s, &QSpinBox::valueChanged, this, &MainWindow::scheduleCapacity);
    }

    // security
    {
        secBox_ = new QGroupBox(tr("Protection"));
        auto* l = new QVBoxLayout(secBox_);
        pwRadio_  = new QRadioButton(tr("Password (Argon2id)"));
        kemRadio_ = new QRadioButton(tr("Recipient key pair (ML-KEM-768)"));
        pwRadio_->setChecked(true);
        l->addWidget(pwRadio_);
        l->addWidget(kemRadio_);

        pwRow_ = new QWidget;
        auto* pl = new QFormLayout(pwRow_);
        pl->setContentsMargins(0, 0, 0, 0);
        passwordEdit_ = new QLineEdit;
        passwordEdit_->setEchoMode(QLineEdit::Password);
        pl->addRow(tr("Password"), passwordEdit_);
        l->addWidget(pwRow_);

        kemRows_ = new QWidget;
        auto* kl = new QFormLayout(kemRows_);
        kl->setContentsMargins(0, 0, 0, 0);
        auto keyRow = [&](QLineEdit*& edit, const QString& label, bool forSave) {
            auto* w = new QWidget;
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            edit = new QLineEdit;
            auto* b = new QPushButton(tr("…"));
            b->setMaximumWidth(32);
            h->addWidget(edit, 1);
            h->addWidget(b);
            kl->addRow(label, w);
            QLineEdit* target = edit;
            connect(b, &QPushButton::clicked, this, [this, target, forSave] { browseKey(target, forSave); });
        };
        keyRow(pubKeyEdit_, tr("Public key (embed)"), false);
        keyRow(privKeyEdit_, tr("Private key (extract)"), false);
        auto* gen = new QPushButton(tr("Generate key pair…"));
        kl->addRow(gen);
        l->addWidget(kemRows_);
        cl->addWidget(secBox_);

        connect(pwRadio_, &QRadioButton::toggled, this, &MainWindow::onAlgoOrModeChanged);
        connect(gen, &QPushButton::clicked, this, &MainWindow::generateKeys);
        connect(passwordEdit_, &QLineEdit::textChanged, this, &MainWindow::updateButtons);
        connect(pubKeyEdit_, &QLineEdit::textChanged, this, &MainWindow::updateButtons);
        connect(privKeyEdit_, &QLineEdit::textChanged, this, &MainWindow::updateButtons);
    }

    // profile
    {
        auto* box = new QGroupBox(tr("Profile (algorithm parameters, no secrets)"));
        auto* h   = new QHBoxLayout(box);
        auto* imp = new QPushButton(tr("Import…"));
        auto* exp = new QPushButton(tr("Export…"));
        h->addWidget(imp);
        h->addWidget(exp);
        cl->addWidget(box);
        connect(imp, &QPushButton::clicked, this, &MainWindow::importProfile);
        connect(exp, &QPushButton::clicked, this, &MainWindow::exportProfile);
    }

    // actions
    {
        auto* h = new QHBoxLayout;
        embedBtn_   = new QPushButton(tr("Embed"));
        extractBtn_ = new QPushButton(tr("Extract"));
        cancelBtn_  = new QPushButton(tr("Cancel"));
        saveStegoBtn_ = new QPushButton(tr("Save stego…"));
        h->addWidget(embedBtn_);
        h->addWidget(extractBtn_);
        h->addWidget(cancelBtn_);
        cl->addLayout(h);
        cl->addWidget(saveStegoBtn_);
        progress_ = new QProgressBar;
        progress_->setRange(0, 100);
        cl->addWidget(progress_);
        connect(embedBtn_, &QPushButton::clicked, this, &MainWindow::doEmbed);
        connect(extractBtn_, &QPushButton::clicked, this, &MainWindow::doExtract);
        connect(cancelBtn_, &QPushButton::clicked, &worker_, &StegoWorker::cancel);
        connect(saveStegoBtn_, &QPushButton::clicked, this, &MainWindow::saveStego);
    }

    // extracted
    {
        auto* box = new QGroupBox(tr("Extracted message"));
        auto* l   = new QVBoxLayout(box);
        extractedView_ = new QPlainTextEdit;
        extractedView_->setReadOnly(true);
        extractedView_->setMaximumHeight(120);
        saveExtractedBtn_ = new QPushButton(tr("Save as file…"));
        l->addWidget(extractedView_);
        l->addWidget(saveExtractedBtn_);
        cl->addWidget(box);
        connect(saveExtractedBtn_, &QPushButton::clicked, this, &MainWindow::saveExtracted);
    }
    cl->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(controls);
    scroll->setMinimumWidth(380);
    split->addWidget(scroll);

    // ---------- right: compare + metrics ----------
    auto* right = new QWidget;
    auto* rl    = new QVBoxLayout(right);
    viewCombo_  = new QComboBox;
    viewCombo_->addItem(tr("Original | Stego (drag the line)"));
    viewCombo_->addItem(tr("LSB plane: original | stego"));
    viewCombo_->addItem(tr("Amplified difference (x64)"));
    compare_ = new CompareWidget;
    metrics_ = new QLabel(tr("Metrics appear after embedding."));
    metrics_->setTextFormat(Qt::RichText);
    metrics_->setWordWrap(true);
    metrics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rl->addWidget(viewCombo_);
    rl->addWidget(compare_, 1);
    rl->addWidget(metrics_);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);

    connect(viewCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshCompare);

    capacityTimer_ = new QTimer(this);
    capacityTimer_->setSingleShot(true);
    capacityTimer_->setInterval(400);
    connect(capacityTimer_, &QTimer::timeout, this, &MainWindow::updateCapacity);

    statusBar()->showMessage(tr("Ready"));
}

// ---- settings <-> widgets ----------------------------------------------------------------------

AlgoSettings MainWindow::settings() const {
    AlgoSettings s;
    s.algo      = algoCombo_->currentData().toString().toStdString();
    s.bits      = bitsSpin_->value();
    s.threshold = thresholdSpin_->value();
    s.pairs     = pairsSpin_->value();
    s.parity    = paritySpin_->value();
    s.strength  = strengthSpin_->value();
    s.mode      = kemRadio_->isChecked() ? Mode::Kem : Mode::Password;
    return s;
}

void MainWindow::applySettings(const AlgoSettings& s) {
    const int idx = algoCombo_->findData(QString::fromStdString(s.algo));
    if (idx >= 0) algoCombo_->setCurrentIndex(idx);
    bitsSpin_->setValue(s.bits);
    thresholdSpin_->setValue(s.threshold);
    pairsSpin_->setValue(s.pairs);
    paritySpin_->setValue(s.parity);
    strengthSpin_->setValue(s.strength);
    (s.mode == Mode::Kem ? kemRadio_ : pwRadio_)->setChecked(true);
    onAlgoOrModeChanged();
}

void MainWindow::onAlgoOrModeChanged() {
    const bool dct = algoCombo_->currentData().toString() == QStringLiteral("block-dct");
    auto setRow = [](QWidget* w, bool visible) {
        w->setVisible(visible);
        if (auto* form = qobject_cast<QFormLayout*>(w->parentWidget() ? w->parentWidget()->layout() : nullptr))
            if (auto* label = form->labelForField(w)) label->setVisible(visible);
    };
    setRow(bitsSpin_, !dct);
    setRow(thresholdSpin_, !dct);
    setRow(pairsSpin_, dct);
    setRow(paritySpin_, dct);
    setRow(strengthSpin_, dct);

    const bool kem = kemRadio_->isChecked();
    pwRow_->setVisible(!kem);
    kemRows_->setVisible(kem);
    scheduleCapacity();
    updateButtons();
}

void MainWindow::scheduleCapacity() { capacityTimer_->start(); }

void MainWindow::updateCapacity() {
    if (cover_.empty()) {
        capacityLabel_->clear();
        return;
    }
    try {
        const AlgoSettings s = settings();
        const auto algo      = makeAlgorithm(s);
        const size_t header  = s.mode == Mode::Kem ? stego::kKemHeaderBytes : stego::kPasswordHeaderBytes;
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        const stego::Carrier carrier = cover_;
        const size_t bytes = stego::maxPayloadBytes(*algo, carrier, header);
        QGuiApplication::restoreOverrideCursor();
        capacityLabel_->setText(tr("Largest message with these settings: %1 bytes").arg(static_cast<qulonglong>(bytes)));
    } catch (const std::exception& e) {
        while (QGuiApplication::overrideCursor()) QGuiApplication::restoreOverrideCursor();
        capacityLabel_->setText(tr("Capacity: %1").arg(QString::fromUtf8(e.what())));
    }
}

// ---- files -------------------------------------------------------------------------------------

bool MainWindow::loadImageInto(cv::Mat& target, const QString& path) {
    const cv::Mat m = cv::imread(path.toUtf8().constData(), cv::IMREAD_UNCHANGED);
    if (m.empty()) {
        QMessageBox::warning(this, tr("Open image"), tr("Cannot read image:\n%1").arg(path));
        return false;
    }
    if (m.depth() != CV_8U || (m.channels() != 1 && m.channels() != 3 && m.channels() != 4)) {
        QMessageBox::warning(this, tr("Open image"), tr("Only 8-bit gray / colour images are supported."));
        return false;
    }
    target = m;
    return true;
}

void MainWindow::openCover() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open cover image"), {}, tr(kImageFilter));
    if (path.isEmpty() || !loadImageInto(cover_, path)) return;
    coverPath_ = path;
    stego_.release();
    coverLabel_->setText(tr("%1: %2 x %3, %4 channel(s)")
                             .arg(QFileInfo(path).fileName())
                             .arg(cover_.cols)
                             .arg(cover_.rows)
                             .arg(cover_.channels()));
    metrics_->setText(tr("Metrics appear after embedding."));
    refreshCompare();
    updateCapacity();
    updateButtons();
}

void MainWindow::openStego() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open stego image"), {}, tr(kImageFilter));
    if (path.isEmpty() || !loadImageInto(stego_, path)) return;
    if (QFileInfo(path).suffix().toLower().startsWith("jp"))
        QMessageBox::information(this, tr("Stego image"),
                                 tr("This is a JPEG. Extraction only works on lossless files that were never re-compressed."));
    statusBar()->showMessage(tr("Stego image loaded: %1").arg(QFileInfo(path).fileName()));
    refreshCompare();
    updateButtons();
}

void MainWindow::saveStego() {
    if (stego_.empty()) return;
    QString path = QFileDialog::getSaveFileName(this, tr("Save stego image"), {},
                                                tr("PNG (*.png);;BMP (*.bmp);;TIFF (*.tif *.tiff)"));
    if (path.isEmpty()) return;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext != "png" && ext != "bmp" && ext != "tif" && ext != "tiff") path += QStringLiteral(".png");  // lossless only
    if (!cv::imwrite(path.toUtf8().constData(), stego_))
        QMessageBox::warning(this, tr("Save"), tr("Cannot write image:\n%1").arg(path));
    else
        statusBar()->showMessage(tr("Saved %1").arg(path));
}

void MainWindow::choosePayloadFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose file to hide"));
    if (path.isEmpty()) return;
    QString err;
    const QByteArray data = readAll(path, &err);
    if (data.isEmpty()) {
        QMessageBox::warning(this, tr("Open file"), err.isEmpty() ? tr("The file is empty.") : err);
        return;
    }
    payloadFile_     = data;
    payloadFileName_ = QFileInfo(path).fileName();
    payloadEdit_->setEnabled(false);
    payloadFileLabel_->setText(tr("%1 (%2 bytes)").arg(payloadFileName_).arg(data.size()));
    updateButtons();
}

void MainWindow::clearPayloadFile() {
    payloadFile_.clear();
    payloadFileName_.clear();
    payloadEdit_->setEnabled(true);
    payloadFileLabel_->clear();
    updateButtons();
}

QByteArray MainWindow::payloadBytes() const {
    return payloadFile_.isEmpty() ? payloadEdit_->toPlainText().toUtf8() : payloadFile_;
}

void MainWindow::browseKey(QLineEdit* target, bool forSave) {
    const QString path = forSave ? QFileDialog::getSaveFileName(this, tr("Key file"))
                                 : QFileDialog::getOpenFileName(this, tr("Key file"), {}, tr("Key files (*.pub *.key);;All files (*)"));
    if (!path.isEmpty()) target->setText(path);
}

void MainWindow::generateKeys() {
    const QString prefix = QFileDialog::getSaveFileName(this, tr("Key pair name (writes NAME.pub and NAME.key)"));
    if (prefix.isEmpty()) return;
    const QString pubPath = prefix + QStringLiteral(".pub"), keyPath = prefix + QStringLiteral(".key");
    if (QFileInfo::exists(pubPath) || QFileInfo::exists(keyPath)) {
        if (QMessageBox::question(this, tr("Overwrite?"), tr("%1 / %2 already exist. Overwrite?").arg(pubPath, keyPath)) !=
            QMessageBox::Yes)
            return;
    }
    try {
        const auto kp = crypto::generateKeyPair();
        const QByteArray pub(reinterpret_cast<const char*>(kp.publicKey.data()), static_cast<qsizetype>(kp.publicKey.size()));
        const QByteArray prv(reinterpret_cast<const char*>(kp.privateKey.data()), static_cast<qsizetype>(kp.privateKey.size()));
        QString err;
        if (!writeAll(pubPath, pub, &err) || !writeAll(keyPath, prv, &err, true)) {
            QMessageBox::warning(this, tr("Generate key pair"), err);
            return;
        }
        pubKeyEdit_->setText(pubPath);
        privKeyEdit_->setText(keyPath);
        statusBar()->showMessage(tr("Key pair written. Keep %1 secret.").arg(keyPath));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Generate key pair"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::importProfile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Import profile"), {}, tr("JSON (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    QString err;
    const QByteArray data = readAll(path, &err);
    if (data.isEmpty()) {
        QMessageBox::warning(this, tr("Import profile"), err.isEmpty() ? tr("The file is empty.") : err);
        return;
    }
    try {
        applySettings(fromProfile(cli::parseProfileJson(data.toStdString()), settings()));
        statusBar()->showMessage(tr("Profile imported"));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Import profile"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::exportProfile() {
    const AlgoSettings s = settings();
    if (const std::string bad = validate(s); !bad.empty()) {
        QMessageBox::warning(this, tr("Export profile"), QString::fromStdString(bad));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export profile"), QStringLiteral("stego-profile.json"),
                                                      tr("JSON (*.json)"));
    if (path.isEmpty()) return;
    QString err;
    if (!writeAll(path, QByteArray::fromStdString(cli::profileToJson(toProfile(s))), &err))
        QMessageBox::warning(this, tr("Export profile"), err);
    else
        statusBar()->showMessage(tr("Profile exported"));
}

// ---- operations --------------------------------------------------------------------------------

void MainWindow::doEmbed() {
    if (cover_.empty()) return;
    const QByteArray payload = payloadBytes();
    if (payload.isEmpty()) {
        QMessageBox::warning(this, tr("Embed"), tr("The message is empty."));
        return;
    }
    const AlgoSettings s = settings();
    std::shared_ptr<const stego::IStegoAlgorithm> algo;
    try {
        algo = makeAlgorithm(s);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Embed"), QString::fromUtf8(e.what()));
        return;
    }

    extracted_.clear();
    extractedView_->clear();
    setBusy(true);
    progress_->setValue(0);
    if (s.mode == Mode::Password) {
        worker_.embedWithPassword(algo, cover_, payload, passwordEdit_->text().toUtf8());
    } else {
        QString err;
        const QByteArray key = readAll(pubKeyEdit_->text(), &err);
        if (key.isEmpty()) {
            setBusy(false);
            QMessageBox::warning(this, tr("Embed"), tr("Cannot read the public key: %1").arg(err));
            return;
        }
        worker_.embed(algo, cover_, payload, key);
    }
    statusBar()->showMessage(tr("Embedding…"));
}

void MainWindow::doExtract() {
    if (stego_.empty()) return;
    const AlgoSettings s = settings();
    std::shared_ptr<const stego::IStegoAlgorithm> algo;
    try {
        algo = makeAlgorithm(s);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Extract"), QString::fromUtf8(e.what()));
        return;
    }
    extracted_.clear();
    extractedView_->clear();
    setBusy(true);
    progress_->setValue(0);
    if (s.mode == Mode::Password) {
        worker_.extractWithPassword(algo, stego_, passwordEdit_->text().toUtf8());
    } else {
        QString err;
        const QByteArray key = readAll(privKeyEdit_->text(), &err);
        if (key.isEmpty()) {
            setBusy(false);
            QMessageBox::warning(this, tr("Extract"), tr("Cannot read the private key: %1").arg(err));
            return;
        }
        worker_.extract(algo, stego_, key);
    }
    statusBar()->showMessage(tr("Extracting…"));
}

void MainWindow::saveExtracted() {
    if (extracted_.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save extracted message"));
    if (path.isEmpty()) return;
    QString err;
    if (!writeAll(path, extracted_, &err)) QMessageBox::warning(this, tr("Save"), err);
}

// ---- worker results ----------------------------------------------------------------------------

void MainWindow::onProgress(int percent) { progress_->setValue(percent); }

void MainWindow::onEmbedFinished(const EmbedReport& r) {
    setBusy(false);
    stego_ = r.stego;
    showMetrics(r);
    refreshCompare();
    updateButtons();
    statusBar()->showMessage(tr("Embedded. Save the stego image as PNG/BMP/TIFF - never as JPEG."));
}

void MainWindow::onExtractFinished(const ExtractReport& r) {
    setBusy(false);
    extracted_ = r.payload;
    if (looksLikeText(extracted_))
        extractedView_->setPlainText(QString::fromUtf8(extracted_));
    else
        extractedView_->setPlainText(tr("(binary data, %1 bytes - use \"Save as file\")").arg(extracted_.size()));
    updateButtons();
    statusBar()->showMessage(tr("Extracted %1 bytes").arg(extracted_.size()));
}

void MainWindow::onFailed(const QString& message) {
    setBusy(false);
    statusBar()->showMessage(tr("Failed"));
    QMessageBox::warning(this, tr("Operation failed"),
                         message + tr("\n\nIf you were extracting: wrong password/key, wrong algorithm parameters, or "
                                      "no message / modified image all look the same by design."));
}

void MainWindow::onCancelled() {
    setBusy(false);
    progress_->setValue(0);
    statusBar()->showMessage(tr("Cancelled"));
}

// ---- display -----------------------------------------------------------------------------------

void MainWindow::showMetrics(const EmbedReport& r) {
    const double pct = r.capacityBits ? 100.0 * static_cast<double>(r.usedBits) / static_cast<double>(r.capacityBits) : 0.0;
    const QString psnr = std::isinf(r.psnr) ? QStringLiteral("inf") : QString::number(r.psnr, 'f', 2) + QStringLiteral(" dB");
    QString html;
    html += tr("<b>Capacity</b>: used %1 of %2 bits (%3 %)<br>").arg(static_cast<qulonglong>(r.usedBits)).arg(static_cast<qulonglong>(r.capacityBits)).arg(pct, 0, 'f', 1);
    html += tr("<b>PSNR</b>: %1 &nbsp; <b>SSIM</b>: %2<br>").arg(psnr).arg(r.ssim, 0, 'f', 5);
    html += tr("<b>Chi-square p(embedded)</b>: cover %1, stego %2 "
               "<i>(global test, blind to low-fill adaptive embedding)</i><br>")
                .arg(r.chiBefore.pEmbedded, 0, 'f', 4)
                .arg(r.chiAfter.pEmbedded, 0, 'f', 4);
    html += tr("<b>RS</b>: cover %1, stego %2 &nbsp; <b>Sample pairs</b>: cover %3, stego %4<br>")
                .arg(lsbText(r.lsbBefore, true), lsbText(r.lsbAfter, true), lsbText(r.lsbBefore, false),
                     lsbText(r.lsbAfter, false));
    html += tr("<i>RS/SPA estimate the fraction of all samples carrying a bit (0 = clean, noise about 0.03; much more "
               "on very noisy images). &quot;!&quot; = many clipped pixels, value probably biased upward. Only LSB "
               "replacement is modelled; Block DCT is invisible to them.</i>");
    metrics_->setText(html);
}

void MainWindow::refreshCompare() {
    if (cover_.empty() && stego_.empty()) {
        compare_->clear();
        return;
    }
    const bool both = !cover_.empty() && !stego_.empty() && cover_.size() == stego_.size() &&
                      cover_.channels() == stego_.channels();
    try {
        switch (viewCombo_->currentIndex()) {
            case 1:
                if (both) {
                    compare_->setImages(toQImage(analysis::lsbPlane(cover_, 0)), toQImage(analysis::lsbPlane(stego_, 0)),
                                        tr("Original LSB"), tr("Stego LSB"));
                    return;
                }
                break;
            case 2:
                if (both) {
                    compare_->setImages(toQImage(analysis::amplifiedDiff(cover_, stego_, 64.0)), QImage(),
                                        tr("|original - stego| x64"));
                    return;
                }
                break;
            default:
                break;
        }
        if (both) compare_->setImages(toQImage(cover_), toQImage(stego_), tr("Original"), tr("Stego"));
        else if (!stego_.empty() && cover_.empty()) compare_->setImages(toQImage(stego_), QImage(), tr("Stego"));
        else compare_->setImages(toQImage(cover_), QImage(), tr("Original"));
    } catch (const std::exception& e) {
        statusBar()->showMessage(QString::fromUtf8(e.what()));
    }
}

void MainWindow::setBusy(bool busy) {
    busy_ = busy;
    updateButtons();
}

void MainWindow::updateButtons() {
    const bool idle = !busy_;
    const bool kem  = kemRadio_ && kemRadio_->isChecked();
    const bool haveSecretForEmbed   = kem ? !pubKeyEdit_->text().isEmpty() : !passwordEdit_->text().isEmpty();
    const bool haveSecretForExtract = kem ? !privKeyEdit_->text().isEmpty() : !passwordEdit_->text().isEmpty();
    const bool havePayload = !payloadBytes().isEmpty();

    openCoverBtn_->setEnabled(idle);
    openStegoBtn_->setEnabled(idle);
    algoBox_->setEnabled(idle);
    secBox_->setEnabled(idle);
    embedBtn_->setEnabled(idle && !cover_.empty() && havePayload && haveSecretForEmbed);
    extractBtn_->setEnabled(idle && !stego_.empty() && haveSecretForExtract);
    cancelBtn_->setEnabled(busy_);
    saveStegoBtn_->setEnabled(idle && !stego_.empty());
    saveExtractedBtn_->setEnabled(idle && !extracted_.isEmpty());
}

}  // namespace app
