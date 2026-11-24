#pragma once

#include <QMainWindow>
#include <QThread>
#include <QElapsedTimer>
#include <memory>

#include "CandidateTableModel.h"
#include "DropAreaTextEdit.h"
#include "../core/Candidate.h"
#include "../core/Scorer.h"
#include "../core/XorEngine.h"

class QTableView;
class QSortFilterProxyModel;
class CustomProxyModel;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QLabel;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onAutoDetectEncoding();
    void onLoadFile();
    void onPasteClipboard();
    void onClearInput();
    void onStartAttack();
    void onCancelAttack();
    void onDetectKeyLengths();

    void onEngineProgress(int percentage, quint64 checked, quint64 total);
    void onEngineFinished(const QVector<Candidate> &results);
    void onEngineError(const QString &errorMessage);
    void onEngineStatus(const QString &message);

    void onTableRowDoubleClicked(const QModelIndex &index);
    void onCopySelectedResult();
    void onSaveSelectedResult();
    void onExportAllCsv();
    void onOpenSettings();
    void onFilterFlagChanged(bool checked);
    void onSearchTextChanged(const QString &text);

private:
    void setupUi();
    void setupEngineThread();
    void applyCurrentTheme();
    Candidate getSelectedCandidate() const;

    // UI Widgets
    DropAreaTextEdit *m_inputEdit = nullptr;
    QComboBox *m_encodingCombo = nullptr;
    QCheckBox *m_reverseBytesCheck = nullptr;
    QLabel *m_inputInfoLabel = nullptr;

    QTabWidget *m_modeTabs = nullptr;
    QSpinBox *m_multiByteLenSpin = nullptr;
    QLabel *m_multiByteWarningLabel = nullptr;
    QSpinBox *m_smartKeyLenSpin = nullptr;
    QLineEdit *m_cribEdit = nullptr;
    QCheckBox *m_cribDragCheck = nullptr;
    QSpinBox *m_knownKeyLenSpin = nullptr;
    QCheckBox *m_comboXorAddCheck = nullptr;
    QCheckBox *m_comboXorRolCheck = nullptr;
    QSpinBox *m_topNSpin = nullptr;

    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    QTableView *m_tableView = nullptr;
    CandidateTableModel *m_tableModel = nullptr;
    CustomProxyModel *m_proxyModel = nullptr;
    QCheckBox *m_onlyFlagCheck = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLabel = nullptr;

    // Engine & Threading
    QThread m_workerThread;
    XorEngine *m_engine = nullptr;
    Scorer m_scorer;
    QElapsedTimer m_timer;
    bool m_isRunning = false;
};
