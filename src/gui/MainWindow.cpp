#include "MainWindow.h"
#include "ResultViewerDialog.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "../core/EncodingUtils.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QTabWidget>
#include <QTableView>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QProgressBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QShortcut>
#include <QKeySequence>
#include <QTextStream>
#include <QSettings>

// Custom proxy model to filter both by search text and "Flag only"
class CustomProxyModel : public QSortFilterProxyModel {
public:
    explicit CustomProxyModel(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setOnlyFlag(bool onlyFlag) {
        if (m_onlyFlag != onlyFlag) {
            beginFilterChange();
            m_onlyFlag = onlyFlag;
            endFilterChange();
        }
    }

    void setSearchFilter(const QString &pattern) {
        QString trimmed = pattern.trimmed();
        if (m_searchPattern != trimmed) {
            beginFilterChange();
            m_searchPattern = trimmed;
            endFilterChange();
        }
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        if (m_onlyFlag) {
            QModelIndex flagIdx = sourceModel()->index(sourceRow, CandidateTableModel::ColFlag, sourceParent);
            if (flagIdx.data(Qt::DisplayRole).toString().isEmpty()) {
                return false;
            }
        }

        if (!m_searchPattern.isEmpty()) {
            QModelIndex previewIdx = sourceModel()->index(sourceRow, CandidateTableModel::ColPreview, sourceParent);
            QModelIndex keyIdx = sourceModel()->index(sourceRow, CandidateTableModel::ColKeyHex, sourceParent);

            QString preview = previewIdx.data(Qt::DisplayRole).toString();
            QString key = keyIdx.data(Qt::DisplayRole).toString();

            if (!preview.contains(m_searchPattern, Qt::CaseInsensitive) &&
                !key.contains(m_searchPattern, Qt::CaseInsensitive)) {
                return false;
            }
        }

        return true;
    }

    bool lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const override {
        // Rank and Score numeric sorting
        if (sourceLeft.column() == CandidateTableModel::ColRank || sourceLeft.column() == CandidateTableModel::ColScore) {
            double v1 = sourceLeft.data(Qt::UserRole).toDouble();
            double v2 = sourceRight.data(Qt::UserRole).toDouble();
            return v1 < v2;
        }
        return QSortFilterProxyModel::lessThan(sourceLeft, sourceRight);
    }

private:
    bool m_onlyFlag = false;
    QString m_searchPattern;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("XOR Brute Force & Cryptanalysis Tool"));
    resize(1100, 750);

    // Load initial settings
    QSettings settings;
    ScorerSettings ss;
    ss.load(settings);
    m_scorer.setSettings(ss);

    setupUi();
    setupEngineThread();
    applyCurrentTheme();
}

MainWindow::~MainWindow() {
    if (m_engine) {
        m_engine->cancel();
    }
    m_workerThread.quit();
    m_workerThread.wait();
}

void MainWindow::applyCurrentTheme() {
    QSettings settings;
    QString theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
    Theme::applyTheme(qobject_cast<QApplication*>(QApplication::instance()), theme);
}

void MainWindow::setupUi() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(10, 10, 10, 5);

    QSplitter *splitter = new QSplitter(Qt::Vertical, centralWidget);
    rootLayout->addWidget(splitter);

    // =========================================================================
    // TOP PANE: Input & Attack Configuration
    // =========================================================================
    QWidget *topWidget = new QWidget(splitter);
    QVBoxLayout *topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 5);

    // Input Group
    QGroupBox *inputGroup = new QGroupBox(tr("Ciphertext Input"), topWidget);
    QVBoxLayout *inputGroupLayout = new QVBoxLayout(inputGroup);

    m_inputEdit = new DropAreaTextEdit(inputGroup);
    m_inputEdit->setMinimumHeight(80);
    inputGroupLayout->addWidget(m_inputEdit);

    // Input Toolbar
    QHBoxLayout *inputToolLayout = new QHBoxLayout();
    QPushButton *loadBtn = new QPushButton(tr("Load from File..."), inputGroup);
    QPushButton *pasteBtn = new QPushButton(tr("Paste"), inputGroup);
    QPushButton *clearBtn = new QPushButton(tr("Clear"), inputGroup);
    QPushButton *detectBtn = new QPushButton(tr("Auto-detect Encoding"), inputGroup);

    QLabel *encLabel = new QLabel(tr("Encoding:"), inputGroup);
    m_encodingCombo = new QComboBox(inputGroup);
    m_encodingCombo->addItem(tr("Auto-detect"), static_cast<int>(EncodingType::Auto));
    m_encodingCombo->addItem(tr("Base64"), static_cast<int>(EncodingType::Base64));
    m_encodingCombo->addItem(tr("Hex"), static_cast<int>(EncodingType::Hex));
    m_encodingCombo->addItem(tr("Base32"), static_cast<int>(EncodingType::Base32));
    m_encodingCombo->addItem(tr("Base85 / ASCII85"), static_cast<int>(EncodingType::Base85));
    m_encodingCombo->addItem(tr("URL-decode"), static_cast<int>(EncodingType::Url));
    m_encodingCombo->addItem(tr("Raw Bytes"), static_cast<int>(EncodingType::Raw));

    m_reverseBytesCheck = new QCheckBox(tr("Reverse bytes"), inputGroup);
    m_reverseBytesCheck->setToolTip(tr("Reverse byte order after decoding input"));

    m_inputInfoLabel = new QLabel(inputGroup);

    inputToolLayout->addWidget(loadBtn);
    inputToolLayout->addWidget(pasteBtn);
    inputToolLayout->addWidget(clearBtn);
    inputToolLayout->addSpacing(15);
    inputToolLayout->addWidget(detectBtn);
    inputToolLayout->addWidget(encLabel);
    inputToolLayout->addWidget(m_encodingCombo);
    inputToolLayout->addWidget(m_reverseBytesCheck);
    inputToolLayout->addSpacing(15);
    inputToolLayout->addWidget(m_inputInfoLabel);
    inputToolLayout->addStretch();

    inputGroupLayout->addLayout(inputToolLayout);
    topLayout->addWidget(inputGroup);

    // Attack Modes Tabs
    m_modeTabs = new QTabWidget(topWidget);

    // Tab 1: Single-byte
    QWidget *singleTab = new QWidget(m_modeTabs);
    QVBoxLayout *singleLayout = new QVBoxLayout(singleTab);
    QLabel *singleInfo = new QLabel(tr("Exhaustive brute force across all 256 single-byte keys (0x00..0xFF). Fast and zero memory overhead."), singleTab);
    singleLayout->addWidget(singleInfo);
    m_modeTabs->addTab(singleTab, tr("Single-byte XOR"));

    // Tab 2: Multi-byte (2-3 bytes)
    QWidget *multiTab = new QWidget(m_modeTabs);
    QVBoxLayout *multiLayout = new QVBoxLayout(multiTab);
    QHBoxLayout *multiRow = new QHBoxLayout();
    QLabel *multiLenLabel = new QLabel(tr("Key Length:"), multiTab);
    m_multiByteLenSpin = new QSpinBox(multiTab);
    m_multiByteLenSpin->setRange(2, 3);
    m_multiByteLenSpin->setValue(2);

    QPushButton *multiDetectBtn = new QPushButton(tr("Auto-detect Key Length (Hamming/IoC)"), multiTab);

    multiRow->addWidget(multiLenLabel);
    multiRow->addWidget(m_multiByteLenSpin);
    multiRow->addWidget(multiDetectBtn);
    multiRow->addStretch();
    multiLayout->addLayout(multiRow);

    m_multiByteWarningLabel = new QLabel(tr("Note: 2 bytes = 65,536 keys; 3 bytes = 16.7M keys (parallelized). For 4+ bytes, use Smart Mode!"), multiTab);
    multiLayout->addWidget(m_multiByteWarningLabel);
    m_modeTabs->addTab(multiTab, tr("Multi-byte XOR (Exhaustive)"));

    // Tab 3: Smart Mode (4+ bytes)
    QWidget *smartTab = new QWidget(m_modeTabs);
    QVBoxLayout *smartLayout = new QVBoxLayout(smartTab);
    QHBoxLayout *smartRow = new QHBoxLayout();
    QLabel *smartLenLabel = new QLabel(tr("Key Length:"), smartTab);
    m_smartKeyLenSpin = new QSpinBox(smartTab);
    m_smartKeyLenSpin->setRange(2, 32);
    m_smartKeyLenSpin->setValue(4);

    QPushButton *smartDetectBtn = new QPushButton(tr("Auto-detect Key Length (Hamming/IoC)"), smartTab);

    smartRow->addWidget(smartLenLabel);
    smartRow->addWidget(m_smartKeyLenSpin);
    smartRow->addWidget(smartDetectBtn);
    smartRow->addStretch();
    smartLayout->addLayout(smartRow);

    QLabel *smartInfo = new QLabel(tr("Smart Mode transposes ciphertext by column and scores each byte position independently. Instant execution even for 32-byte keys."), smartTab);
    smartLayout->addWidget(smartInfo);
    m_modeTabs->addTab(smartTab, tr("Smart Mode (Transposition)"));

    // Tab 4: Known Plaintext / Crib Dragging
    QWidget *knownTab = new QWidget(m_modeTabs);
    QVBoxLayout *knownLayout = new QVBoxLayout(knownTab);
    QHBoxLayout *knownRow = new QHBoxLayout();

    QLabel *cribLabel = new QLabel(tr("Known Prefix / Substring:"), knownTab);
    m_cribEdit = new QLineEdit(knownTab);
    m_cribEdit->setPlaceholderText(tr("e.g. FLAG{ or CTF{"));
    m_cribEdit->setText(QStringLiteral("FLAG{"));

    m_cribDragCheck = new QCheckBox(tr("Sliding Crib Dragging (search everywhere)"), knownTab);
    m_cribDragCheck->setChecked(true);

    QLabel *assumedLenLabel = new QLabel(tr("Assumed Key Length (0 for auto):"), knownTab);
    m_knownKeyLenSpin = new QSpinBox(knownTab);
    m_knownKeyLenSpin->setRange(0, 32);
    m_knownKeyLenSpin->setValue(0);

    knownRow->addWidget(cribLabel);
    knownRow->addWidget(m_cribEdit);
    knownRow->addWidget(m_cribDragCheck);
    knownRow->addWidget(assumedLenLabel);
    knownRow->addWidget(m_knownKeyLenSpin);
    knownLayout->addLayout(knownRow);
    m_modeTabs->addTab(knownTab, tr("Known-Plaintext (Crib Dragging)"));

    // Tab 5: ADD/SUB & ROT-N
    QWidget *addRotTab = new QWidget(m_modeTabs);
    QVBoxLayout *addRotLayout = new QVBoxLayout(addRotTab);
    QLabel *addRotInfo = new QLabel(tr("Tests all 256 byte-wise ADD/SUB shifts ((b - k) & 0xFF), Caesar ROT-1..25 on letters, and ROT-47 on printable ASCII."), addRotTab);
    addRotLayout->addWidget(addRotInfo);
    m_modeTabs->addTab(addRotTab, tr("ADD/SUB & ROT-N"));

    // Tab 6: Bitwise Shifts + NOT
    QWidget *bitwiseTab = new QWidget(m_modeTabs);
    QVBoxLayout *bitwiseLayout = new QVBoxLayout(bitwiseTab);
    QLabel *bitwiseInfo = new QLabel(tr("Tests bitwise NOT (~b), ROL/ROR (1-7 bits), and their combinations with single-byte XOR (e.g. ROL(b, r) ^ k, (~b) ^ k)."), bitwiseTab);
    bitwiseLayout->addWidget(bitwiseInfo);
    m_modeTabs->addTab(bitwiseTab, tr("Bitwise (ROL/ROR/NOT)"));

    // Tab 7: Combo (XOR + ADD/ROL)
    QWidget *comboTab = new QWidget(m_modeTabs);
    QVBoxLayout *comboLayout = new QVBoxLayout(comboTab);
    QHBoxLayout *comboRow = new QHBoxLayout();
    m_comboXorAddCheck = new QCheckBox(tr("XOR + ADD/SUB (65,536 x 2 combinations)"), comboTab);
    m_comboXorAddCheck->setChecked(true);
    m_comboXorRolCheck = new QCheckBox(tr("XOR + ROL/ROR (1-7 bits, both orders)"), comboTab);
    m_comboXorRolCheck->setChecked(true);
    comboRow->addWidget(m_comboXorAddCheck);
    comboRow->addSpacing(20);
    comboRow->addWidget(m_comboXorRolCheck);
    comboRow->addStretch();
    comboLayout->addLayout(comboRow);
    QLabel *comboInfo = new QLabel(tr("Parallelized combo brute force across both operation orders: (b ^ k1) op k2 and (b op k2) ^ k1."), comboTab);
    comboLayout->addWidget(comboInfo);
    m_modeTabs->addTab(comboTab, tr("Combo (XOR + ADD/ROL)"));

    topLayout->addWidget(m_modeTabs);

    // Launch Controls Bar
    QHBoxLayout *launchLayout = new QHBoxLayout();
    m_startBtn = new QPushButton(tr("Start Attack (Ctrl+Enter)"), topWidget);
    m_startBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_startBtn->setMinimumHeight(34);

    m_cancelBtn = new QPushButton(tr("Cancel"), topWidget);
    m_cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
    m_cancelBtn->setMinimumHeight(34);
    m_cancelBtn->setEnabled(false);

    QLabel *topNLabel = new QLabel(tr("Top Candidates:"), topWidget);
    m_topNSpin = new QSpinBox(topWidget);
    m_topNSpin->setRange(10, 1000);
    m_topNSpin->setValue(200);

    launchLayout->addWidget(m_startBtn);
    launchLayout->addWidget(m_cancelBtn);
    launchLayout->addSpacing(20);
    launchLayout->addWidget(topNLabel);
    launchLayout->addWidget(m_topNSpin);
    launchLayout->addStretch();

    topLayout->addLayout(launchLayout);
    splitter->addWidget(topWidget);

    // =========================================================================
    // BOTTOM PANE: Results Table & Actions
    // =========================================================================
    QWidget *bottomWidget = new QWidget(splitter);
    QVBoxLayout *bottomLayout = new QVBoxLayout(bottomWidget);
    bottomLayout->setContentsMargins(0, 5, 0, 0);

    // Results Filter / Action Bar
    QHBoxLayout *tableToolbar = new QHBoxLayout();
    m_onlyFlagCheck = new QCheckBox(tr("Only with FLAG"), bottomWidget);

    QLabel *searchLabel = new QLabel(tr("Search:"), bottomWidget);
    m_searchEdit = new QLineEdit(bottomWidget);
    m_searchEdit->setPlaceholderText(tr("Filter results preview or hex..."));

    QPushButton *copyBtn = new QPushButton(tr("Copy Result (Ctrl+C)"), bottomWidget);
    QPushButton *saveBtn = new QPushButton(tr("Save Result to File (Ctrl+S)"), bottomWidget);
    QPushButton *exportCsvBtn = new QPushButton(tr("Export CSV..."), bottomWidget);
    QPushButton *settingsBtn = new QPushButton(tr("Settings..."), bottomWidget);

    tableToolbar->addWidget(m_onlyFlagCheck);
    tableToolbar->addSpacing(10);
    tableToolbar->addWidget(searchLabel);
    tableToolbar->addWidget(m_searchEdit);
    tableToolbar->addSpacing(15);
    tableToolbar->addWidget(copyBtn);
    tableToolbar->addWidget(saveBtn);
    tableToolbar->addWidget(exportCsvBtn);
    tableToolbar->addWidget(settingsBtn);

    bottomLayout->addLayout(tableToolbar);

    // Results Table View
    m_tableView = new QTableView(bottomWidget);
    m_tableModel = new CandidateTableModel(this);
    auto *proxy = new CustomProxyModel(this);
    proxy->setSourceModel(m_tableModel);
    m_proxyModel = proxy;

    m_tableView->setModel(m_proxyModel);
    m_tableView->setSortingEnabled(true);
    m_tableView->sortByColumn(CandidateTableModel::ColRank, Qt::AscendingOrder);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(CandidateTableModel::ColPreview, QHeaderView::Stretch);
    m_tableView->setColumnWidth(CandidateTableModel::ColRank, 65);
    m_tableView->setColumnWidth(CandidateTableModel::ColKeyHex, 100);
    m_tableView->setColumnWidth(CandidateTableModel::ColKeyAscii, 90);
    m_tableView->setColumnWidth(CandidateTableModel::ColScore, 75);
    m_tableView->setColumnWidth(CandidateTableModel::ColFlag, 60);

    bottomLayout->addWidget(m_tableView);
    splitter->addWidget(bottomWidget);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // =========================================================================
    // Status Bar & Progress
    // =========================================================================
    QStatusBar *bar = statusBar();
    m_statusLabel = new QLabel(tr("Ready."), this);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumWidth(200);
    m_progressBar->setVisible(false);

    bar->addWidget(m_statusLabel, 1);
    bar->addPermanentWidget(m_progressBar);

    // =========================================================================
    // Connections & Shortcuts
    // =========================================================================
    connect(loadBtn, &QPushButton::clicked, this, &MainWindow::onLoadFile);
    connect(pasteBtn, &QPushButton::clicked, this, &MainWindow::onPasteClipboard);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearInput);
    connect(detectBtn, &QPushButton::clicked, this, &MainWindow::onAutoDetectEncoding);
    connect(multiDetectBtn, &QPushButton::clicked, this, &MainWindow::onDetectKeyLengths);
    connect(smartDetectBtn, &QPushButton::clicked, this, &MainWindow::onDetectKeyLengths);

    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartAttack);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelAttack);

    connect(m_onlyFlagCheck, &QCheckBox::toggled, this, &MainWindow::onFilterFlagChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::onTableRowDoubleClicked);
    connect(copyBtn, &QPushButton::clicked, this, &MainWindow::onCopySelectedResult);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveSelectedResult);
    connect(exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportAllCsv);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::onOpenSettings);

    // Shortcuts
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this, SLOT(onStartAttack()));
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this, SLOT(onStartAttack()));
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), this, SLOT(onCopySelectedResult()));
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this, SLOT(onSaveSelectedResult()));
    new QShortcut(QKeySequence(Qt::Key_Escape), this, SLOT(onCancelAttack()));
}

void MainWindow::setupEngineThread() {
    m_engine = new XorEngine();
    m_engine->setScorer(m_scorer);
    m_engine->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::finished, m_engine, &QObject::deleteLater);

    connect(m_engine, &XorEngine::progress, this, &MainWindow::onEngineProgress);
    connect(m_engine, &XorEngine::finished, this, &MainWindow::onEngineFinished);
    connect(m_engine, &XorEngine::error, this, &MainWindow::onEngineError);
    connect(m_engine, &XorEngine::statusMessage, this, &MainWindow::onEngineStatus);

    m_workerThread.start();
}

void MainWindow::onLoadFile() {
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Ciphertext File"), QString(), tr("All Files (*.*)"));
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not read file: %1").arg(file.errorString()));
        return;
    }

    QByteArray data = file.readAll();
    QFileInfo info(fileName);
    m_inputEdit->setRawData(data, info.fileName());
    onAutoDetectEncoding();
}

void MainWindow::onPasteClipboard() {
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.isEmpty()) {
        m_inputEdit->setPlainText(text);
        onAutoDetectEncoding();
    }
}

void MainWindow::onClearInput() {
    m_inputEdit->clearAll();
    m_inputInfoLabel->clear();
    m_tableModel->clear();
    m_statusLabel->setText(tr("Ready."));
}

void MainWindow::onAutoDetectEncoding() {
    QByteArray raw = m_inputEdit->rawData();
    if (raw.isEmpty()) {
        m_inputInfoLabel->setText(tr("(Empty input)"));
        return;
    }

    EncodingType detected = EncodingType::Raw;
    if (!m_inputEdit->isBinaryLoaded()) {
        detected = EncodingUtils::detectEncoding(m_inputEdit->toPlainText());
    }

    int idx = m_encodingCombo->findData(static_cast<int>(detected));
    if (idx >= 0) {
        m_encodingCombo->setCurrentIndex(idx);
    }

    m_inputInfoLabel->setText(tr("Detected: <b>%1</b> (%2 bytes)")
                                  .arg(EncodingUtils::encodingName(detected))
                                  .arg(raw.size()));
}

void MainWindow::onDetectKeyLengths() {
    QByteArray raw = m_inputEdit->rawData();
    if (raw.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please provide ciphertext first."));
        return;
    }

    EncodingType enc = static_cast<EncodingType>(m_encodingCombo->currentData().toInt());
    bool rev = m_reverseBytesCheck->isChecked();
    QByteArray ciphertext = EncodingUtils::decodeInput(m_inputEdit->toPlainText(), enc, raw, rev);

    if (ciphertext.size() < 8) {
        QMessageBox::warning(this, tr("Warning"), tr("Ciphertext is too short for key length estimation (need >= 8 bytes)."));
        return;
    }

    auto estimates = XorEngine::estimateKeyLengths(ciphertext, 32);
    if (estimates.isEmpty()) {
        QMessageBox::information(this, tr("Key Lengths"), tr("Could not determine key lengths."));
        return;
    }

    QString msg = tr("Top candidate key lengths (Normalized Hamming & Index of Coincidence):\n\n");
    for (int i = 0; i < std::min<qsizetype>(5, estimates.size()); ++i) {
        msg += tr("  • Rank #%1: Key Length = %2 (Score: %3)\n")
                   .arg(i + 1)
                   .arg(estimates[i].first)
                   .arg(QString::number(estimates[i].second, 'f', 1));
    }
    msg += tr("\nWould you like to set the key length to %1?").arg(estimates.first().first);

    if (QMessageBox::question(this, tr("Key Length Estimation"), msg) == QMessageBox::Yes) {
        int bestLen = estimates.first().first;
        if (m_modeTabs->currentIndex() == 1) {
            if (bestLen <= 3) {
                m_multiByteLenSpin->setValue(bestLen);
            } else {
                m_modeTabs->setCurrentIndex(2); // Switch to Smart Mode
                m_smartKeyLenSpin->setValue(bestLen);
            }
        } else {
            m_smartKeyLenSpin->setValue(bestLen);
        }
    }
}

void MainWindow::onStartAttack() {
    if (m_isRunning) return;

    QByteArray raw = m_inputEdit->rawData();
    if (raw.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please provide ciphertext input."));
        return;
    }

    EncodingType enc = static_cast<EncodingType>(m_encodingCombo->currentData().toInt());
    bool rev = m_reverseBytesCheck->isChecked();
    QByteArray ciphertext = EncodingUtils::decodeInput(m_inputEdit->toPlainText(), enc, raw, rev);

    if (ciphertext.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Decoded ciphertext is empty. Please verify encoding."));
        return;
    }

    int topN = m_topNSpin->value();
    int modeIdx = m_modeTabs->currentIndex();

    m_isRunning = true;
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_tableModel->clear();
    m_timer.restart();

    if (modeIdx == 0) {
        // Single-byte
        QMetaObject::invokeMethod(m_engine, "runSingleByte", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, ciphertext), Q_ARG(int, topN));
    } else if (modeIdx == 1) {
        // Multi-byte (2-3 bytes)
        int keyLen = m_multiByteLenSpin->value();
        QMetaObject::invokeMethod(m_engine, "runMultiByte", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, ciphertext), Q_ARG(int, keyLen), Q_ARG(int, topN));
    } else if (modeIdx == 2) {
        // Smart Mode (4+ bytes)
        int keyLen = m_smartKeyLenSpin->value();
        QMetaObject::invokeMethod(m_engine, "runSmartMultiByte", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, ciphertext), Q_ARG(int, keyLen), Q_ARG(int, topN));
    } else if (modeIdx == 3) {
        // Known-Plaintext
        QByteArray crib = m_cribEdit->text().toUtf8();
        bool cribDrag = m_cribDragCheck->isChecked();
        int assumedLen = m_knownKeyLenSpin->value();
        QMetaObject::invokeMethod(m_engine, "runKnownPlaintext", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, ciphertext), Q_ARG(QByteArray, crib),
                                  Q_ARG(int, assumedLen), Q_ARG(bool, cribDrag), Q_ARG(int, topN));
    } else if (modeIdx == 4) {
        // ADD/SUB & ROT-N
        QMetaObject::invokeMethod(m_engine, "runAddSubRot", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, ciphertext), Q_ARG(int, topN));
    } else if (modeIdx == 5) {
        // Bitwise ROL/ROR/NOT
        QMetaObject::invokeMethod(m_engine, "runBitwiseShiftNot", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, ciphertext), Q_ARG(int, topN));
    } else if (modeIdx == 6) {
        // Combo XOR + ADD/ROL
        bool doAdd = m_comboXorAddCheck->isChecked();
        bool doRol = m_comboXorRolCheck->isChecked();
        QMetaObject::invokeMethod(m_engine, "runComboXorAddRol", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, ciphertext), Q_ARG(bool, doAdd), Q_ARG(bool, doRol), Q_ARG(int, topN));
    }
}

void MainWindow::onCancelAttack() {
    if (m_isRunning && m_engine) {
        m_engine->cancel();
        m_statusLabel->setText(tr("Cancelling..."));
    }
}

void MainWindow::onEngineProgress(int percentage, quint64 checked, quint64 total) {
    m_progressBar->setValue(percentage);
    m_statusLabel->setText(tr("Checking keys: %1 / %2 (%3%)")
                               .arg(checked).arg(total).arg(percentage));
}

void MainWindow::onEngineFinished(const QVector<Candidate> &results) {
    m_isRunning = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);

    qint64 elapsedMs = m_timer.elapsed();
    m_tableModel->setCandidates(results);

    m_statusLabel->setText(tr("Completed in %1 ms. %2 candidates displayed.")
                               .arg(elapsedMs).arg(results.size()));
}

void MainWindow::onEngineError(const QString &errorMessage) {
    m_isRunning = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);

    m_statusLabel->setText(tr("Error: %1").arg(errorMessage));
    QMessageBox::critical(this, tr("Engine Error"), errorMessage);
}

void MainWindow::onEngineStatus(const QString &message) {
    m_statusLabel->setText(message);
}

Candidate MainWindow::getSelectedCandidate() const {
    QModelIndex proxyIdx = m_tableView->currentIndex();
    if (!proxyIdx.isValid()) return Candidate();

    QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
    return m_tableModel->candidateAt(srcIdx.row());
}

void MainWindow::onTableRowDoubleClicked(const QModelIndex &index) {
    Q_UNUSED(index);
    Candidate c = getSelectedCandidate();
    if (c.rank > 0) {
        ResultViewerDialog dlg(c, this);
        dlg.exec();
    }
}

void MainWindow::onCopySelectedResult() {
    Candidate c = getSelectedCandidate();
    if (c.rank > 0) {
        QGuiApplication::clipboard()->setText(QString::fromUtf8(c.plaintext));
        m_statusLabel->setText(tr("Candidate #%1 plaintext copied to clipboard.").arg(c.rank));
    }
}

void MainWindow::onSaveSelectedResult() {
    Candidate c = getSelectedCandidate();
    if (c.rank <= 0) {
        QMessageBox::warning(this, tr("Warning"), tr("Please select a candidate in the table first."));
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Decrypted Output"), QString(), tr("All Files (*.*)"));
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write file: %1").arg(file.errorString()));
        return;
    }
    file.write(c.plaintext);
    file.close();
    m_statusLabel->setText(tr("Saved candidate #%1 to %2").arg(c.rank).arg(fileName));
}

void MainWindow::onExportAllCsv() {
    const auto &candidates = m_tableModel->allCandidates();
    if (candidates.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("No candidates to export."));
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Candidates to CSV"), QStringLiteral("xor_candidates.csv"), tr("CSV Files (*.csv)"));
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write CSV: %1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out << "Rank,KeyHex,Score,Flag,Preview\n";

    for (const auto &c : candidates) {
        QString escapedPreview = c.preview;
        escapedPreview.replace('"', QStringLiteral("\"\""));
        QString keyStr = c.keyDesc.isEmpty() ? QString::fromLatin1(c.key.toHex().toUpper()) : c.keyDesc;
        keyStr.replace('"', QStringLiteral("\"\""));
        out << c.rank << ","
            << "\"" << keyStr << "\","
            << QString::number(c.score, 'f', 1) << ","
            << (c.containsFlag ? "YES" : "NO") << ","
            << "\"" << escapedPreview << "\"\n";
    }

    file.close();
    m_statusLabel->setText(tr("Exported %1 candidates to %2").arg(candidates.size()).arg(fileName));
    QMessageBox::information(this, tr("Exported"), tr("Successfully exported %1 candidates to CSV.").arg(candidates.size()));
}

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        m_scorer.setSettings(dlg.scorerSettings());
        if (m_engine) {
            m_engine->setScorer(m_scorer);
        }
        applyCurrentTheme();
        m_statusLabel->setText(tr("Settings updated."));
    }
}

void MainWindow::onFilterFlagChanged(bool checked) {
    if (m_proxyModel) {
        m_proxyModel->setOnlyFlag(checked);
    }
}

void MainWindow::onSearchTextChanged(const QString &text) {
    if (m_proxyModel) {
        m_proxyModel->setSearchFilter(text);
    }
}
