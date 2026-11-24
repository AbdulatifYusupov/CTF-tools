#include "ResultViewerDialog.h"
#include "../core/EncodingUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFontDatabase>
#include <QClipboard>
#include <QGuiApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>

ResultViewerDialog::ResultViewerDialog(const Candidate &candidate, QWidget *parent)
    : QDialog(parent)
    , m_candidate(candidate)
{
    setWindowTitle(tr("Candidate Details - Rank #%1").arg(candidate.rank));
    resize(750, 550);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Header Info Bar
    QHBoxLayout *headerLayout = new QHBoxLayout();
    QLabel *rankLabel = new QLabel(tr("<b>Rank:</b> #%1").arg(candidate.rank), this);
    QLabel *keyLabel = new QLabel(tr("<b>Key (Hex):</b> <code>%1</code> (len %2)")
                                      .arg(QString::fromLatin1(candidate.key.toHex().toUpper()))
                                      .arg(candidate.key.size()), this);
    QLabel *scoreLabel = new QLabel(tr("<b>Score:</b> %1").arg(QString::number(candidate.score, 'f', 1)), this);

    headerLayout->addWidget(rankLabel);
    headerLayout->addSpacing(15);
    headerLayout->addWidget(keyLabel);
    headerLayout->addSpacing(15);
    headerLayout->addWidget(scoreLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Monospace Font
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(10);

    // Tabs
    m_tabs = new QTabWidget(this);

    // 1. Plaintext Tab
    m_plaintextEdit = new QPlainTextEdit(this);
    m_plaintextEdit->setFont(monoFont);
    m_plaintextEdit->setReadOnly(true);
    m_plaintextEdit->setPlainText(QString::fromUtf8(candidate.plaintext));
    m_tabs->addTab(m_plaintextEdit, tr("Plaintext (UTF-8)"));

    // 2. Hex Dump Tab
    m_hexDumpEdit = new QPlainTextEdit(this);
    m_hexDumpEdit->setFont(monoFont);
    m_hexDumpEdit->setReadOnly(true);
    m_hexDumpEdit->setWordWrapMode(QTextOption::NoWrap);
    m_hexDumpEdit->setPlainText(EncodingUtils::toHexDump(candidate.plaintext));
    m_tabs->addTab(m_hexDumpEdit, tr("Hex Dump"));

    // 3. Escaped Repr Tab
    m_reprEdit = new QPlainTextEdit(this);
    m_reprEdit->setFont(monoFont);
    m_reprEdit->setReadOnly(true);
    m_reprEdit->setPlainText(EncodingUtils::toEscapedRepr(candidate.plaintext, 4096));
    m_tabs->addTab(m_reprEdit, tr("Python / Escaped Repr"));

    mainLayout->addWidget(m_tabs);

    // Action Buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *copyTextBtn = new QPushButton(tr("Copy Plaintext"), this);
    QPushButton *copyHexBtn = new QPushButton(tr("Copy Hex Dump"), this);
    QPushButton *saveBtn = new QPushButton(tr("Save to File..."), this);
    QPushButton *closeBtn = new QPushButton(tr("Close"), this);

    btnLayout->addWidget(copyTextBtn);
    btnLayout->addWidget(copyHexBtn);
    btnLayout->addWidget(saveBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(copyTextBtn, &QPushButton::clicked, this, &ResultViewerDialog::copyPlaintext);
    connect(copyHexBtn, &QPushButton::clicked, this, &ResultViewerDialog::copyHex);
    connect(saveBtn, &QPushButton::clicked, this, &ResultViewerDialog::saveToFile);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void ResultViewerDialog::copyCurrentTab() {
    QWidget *current = m_tabs->currentWidget();
    if (auto *edit = qobject_cast<QPlainTextEdit*>(current)) {
        QGuiApplication::clipboard()->setText(edit->toPlainText());
    }
}

void ResultViewerDialog::copyPlaintext() {
    QGuiApplication::clipboard()->setText(QString::fromUtf8(m_candidate.plaintext));
}

void ResultViewerDialog::copyHex() {
    QGuiApplication::clipboard()->setText(m_hexDumpEdit->toPlainText());
}

void ResultViewerDialog::saveToFile() {
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Decrypted Output"), QString(), tr("All Files (*.*)"));
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to write to file: %1").arg(file.errorString()));
        return;
    }
    file.write(m_candidate.plaintext);
    file.close();
    QMessageBox::information(this, tr("Saved"), tr("Successfully saved decrypted candidate to %1").arg(fileName));
}
