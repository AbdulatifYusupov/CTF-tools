#include "DropAreaTextEdit.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QFile>

DropAreaTextEdit::DropAreaTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setAcceptDrops(true);
    setPlaceholderText(tr("Paste ciphertext here or drag & drop a file..."));

    // If user modifies text manually, clear the binary cache
    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_isBinary && !m_rawBytes.isEmpty()) {
            m_rawBytes.clear();
        }
    });
}

void DropAreaTextEdit::setRawData(const QByteArray &data, const QString &sourceName) {
    m_rawBytes = data;

    // Check if data is clean printable UTF-8 or binary
    bool hasNonPrintable = false;
    for (int i = 0; i < std::min<qsizetype>(data.size(), 1024); ++i) {
        uint8_t b = static_cast<uint8_t>(data.at(i));
        if (b < 32 && b != '\t' && b != '\n' && b != '\r') {
            hasNonPrintable = true;
            break;
        }
    }

    if (hasNonPrintable) {
        m_isBinary = true;
        QString name = sourceName.isEmpty() ? tr("Binary Buffer") : sourceName;
        setPlainText(tr("[Binary Data Loaded: %1 (%2 bytes)]\n(Raw bytes are stored securely without UTF-8 corruption. Select Raw or Auto mode.)")
                         .arg(name).arg(data.size()));
    } else {
        m_isBinary = false;
        setPlainText(QString::fromUtf8(data));
    }
}

QByteArray DropAreaTextEdit::rawData() const {
    if (!m_rawBytes.isEmpty()) {
        return m_rawBytes;
    }
    return toPlainText().toUtf8();
}

void DropAreaTextEdit::clearAll() {
    m_rawBytes.clear();
    m_isBinary = false;
    clear();
}

void DropAreaTextEdit::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    } else {
        QPlainTextEdit::dragEnterEvent(event);
    }
}

void DropAreaTextEdit::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    } else {
        QPlainTextEdit::dragMoveEvent(event);
    }
}

void DropAreaTextEdit::dropEvent(QDropEvent *event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            QString filePath = urls.first().toLocalFile();
            if (!filePath.isEmpty()) {
                QFile file(filePath);
                if (file.open(QIODevice::ReadOnly)) {
                    QByteArray content = file.readAll();
                    QFileInfo info(filePath);
                    setRawData(content, info.fileName());
                    emit fileLoaded(info.fileName(), content);
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }

    if (event->mimeData()->hasText()) {
        m_rawBytes.clear();
        m_isBinary = false;
        setPlainText(event->mimeData()->text());
        event->acceptProposedAction();
        return;
    }

    QPlainTextEdit::dropEvent(event);
}
