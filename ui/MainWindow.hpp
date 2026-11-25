#pragma once
#include "AlgoSettings.hpp"
#include "StegoWorker.hpp"

#include <QByteArray>
#include <QMainWindow>

#include <opencv2/core.hpp>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTimer;

namespace app {

class CompareWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openCover();
    void openStego();
    void saveStego();
    void choosePayloadFile();
    void clearPayloadFile();
    void browseKey(QLineEdit* target, bool forSave);
    void generateKeys();
    void importProfile();
    void exportProfile();
    void doEmbed();
    void doExtract();
    void saveExtracted();

    void onAlgoOrModeChanged();
    void scheduleCapacity();
    void updateCapacity();
    void refreshCompare();

    void onProgress(int percent);
    void onEmbedFinished(const app::EmbedReport& r);
    void onExtractFinished(const app::ExtractReport& r);
    void onFailed(const QString& message);
    void onCancelled();

private:
    void buildUi();
    AlgoSettings settings() const;
    void applySettings(const AlgoSettings& s);
    QByteArray payloadBytes() const;
    void setBusy(bool busy);
    void updateButtons();
    void showMetrics(const EmbedReport& r);
    bool loadImageInto(cv::Mat& target, const QString& path);

    StegoWorker worker_;

    // state
    cv::Mat cover_, stego_;
    QString coverPath_;
    QByteArray payloadFile_;
    QString payloadFileName_;
    QByteArray extracted_;
    bool busy_ = false;

    // widgets
    CompareWidget* compare_ = nullptr;
    QComboBox* viewCombo_   = nullptr;
    QLabel* metrics_        = nullptr;
    QLabel* coverLabel_     = nullptr;
    QLabel* capacityLabel_  = nullptr;

    QPlainTextEdit* payloadEdit_ = nullptr;
    QLabel* payloadFileLabel_    = nullptr;
    QPushButton* clearFileBtn_   = nullptr;

    QGroupBox* algoBox_       = nullptr;
    QComboBox* algoCombo_     = nullptr;
    QSpinBox *bitsSpin_ = nullptr, *thresholdSpin_ = nullptr, *pairsSpin_ = nullptr,
             *paritySpin_ = nullptr, *strengthSpin_ = nullptr;

    QGroupBox* secBox_         = nullptr;
    QRadioButton* pwRadio_     = nullptr;
    QRadioButton* kemRadio_    = nullptr;
    QLineEdit* passwordEdit_   = nullptr;
    QLineEdit* pubKeyEdit_     = nullptr;
    QLineEdit* privKeyEdit_    = nullptr;
    QWidget* pwRow_            = nullptr;
    QWidget* kemRows_          = nullptr;

    QPushButton *openCoverBtn_ = nullptr, *openStegoBtn_ = nullptr, *saveStegoBtn_ = nullptr,
                *embedBtn_ = nullptr, *extractBtn_ = nullptr, *cancelBtn_ = nullptr,
                *saveExtractedBtn_ = nullptr;
    QPlainTextEdit* extractedView_ = nullptr;
    QProgressBar* progress_        = nullptr;
    QTimer* capacityTimer_         = nullptr;
};

}  // namespace app
