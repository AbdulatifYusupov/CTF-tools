#pragma once

#include <QPlainTextEdit>
#include <QByteArray>

class DropAreaTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit DropAreaTextEdit(QWidget *parent = nullptr);

    void setRawData(const QByteArray &data, const QString &sourceName = QString());
    QByteArray rawData() const;
    bool isBinaryLoaded() const { return m_isBinary; }
    void clearAll();

signals:
    void fileLoaded(const QString &fileName, const QByteArray &data);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QByteArray m_rawBytes;
    bool m_isBinary = false;
};
