#pragma once

#include <QDialog>
#include "../core/Candidate.h"

class QTabWidget;
class QPlainTextEdit;

class ResultViewerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ResultViewerDialog(const Candidate &candidate, QWidget *parent = nullptr);

private slots:
    void copyCurrentTab();
    void copyPlaintext();
    void copyHex();
    void saveToFile();

private:
    Candidate m_candidate;
    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_plaintextEdit = nullptr;
    QPlainTextEdit *m_hexDumpEdit = nullptr;
    QPlainTextEdit *m_reprEdit = nullptr;
};
