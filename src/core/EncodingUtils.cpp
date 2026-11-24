#include "EncodingUtils.h"
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

namespace EncodingUtils {

QString cleanInput(const QString &input) {
    QString cleaned = input.trimmed();
    cleaned.remove(QChar('\r'));
    cleaned.remove(QChar('\n'));
    cleaned.remove(QChar(' '));
    cleaned.remove(QChar('\t'));
    return cleaned;
}

EncodingType detectEncoding(const QString &input) {
    QString cleaned = cleanInput(input);
    if (cleaned.isEmpty()) {
        return EncodingType::Raw;
    }

    // Handle 0x prefix if present
    QString hexCandidate = cleaned;
    if (hexCandidate.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        hexCandidate = hexCandidate.mid(2);
    }

    // 1. Check HEX first (e.g. "deadbeef", "414243")
    // Hex must have even length and contain exclusively hex digits
    if (!hexCandidate.isEmpty() && (hexCandidate.length() % 2 == 0)) {
        bool isHex = true;
        for (const QChar &ch : hexCandidate) {
            char c = ch.toLatin1();
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                isHex = false;
                break;
            }
        }
        if (isHex) {
            return EncodingType::Hex;
        }
    }

    // 2. Check BASE64
    // Base64 chars: A-Z, a-z, 0-9, +, /, and = padding at the end
    if (cleaned.length() >= 4) {
        bool validB64Chars = true;
        int equalCount = 0;
        int len = cleaned.length();

        for (int i = 0; i < len; ++i) {
            char c = cleaned.at(i).toLatin1();
            if (c == '=') {
                equalCount++;
                // Padding can only be at the end (up to 2 chars)
                if (i < len - 2) {
                    validB64Chars = false;
                    break;
                }
            } else if (equalCount > 0) {
                // Char after '=' is invalid
                validB64Chars = false;
                break;
            } else {
                bool ok = (c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') ||
                          (c == '+' || c == '/');
                if (!ok) {
                    validB64Chars = false;
                    break;
                }
            }
        }

        if (validB64Chars && (len % 4 == 0)) {
            // Verify decoding does not fail
            QByteArray decoded = QByteArray::fromBase64(cleaned.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded.isEmpty()) {
                return EncodingType::Base64;
            }
        }
    }

    return EncodingType::Raw;
}

QByteArray decodeInput(const QString &input, EncodingType type, const QByteArray &fallbackRaw) {
    if (type == EncodingType::Auto) {
        type = detectEncoding(input);
    }

    QString cleaned = cleanInput(input);

    switch (type) {
    case EncodingType::Hex: {
        QString hexStr = cleaned;
        if (hexStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            hexStr = hexStr.mid(2);
        }
        return QByteArray::fromHex(hexStr.toLatin1());
    }
    case EncodingType::Base64:
        return QByteArray::fromBase64(cleaned.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    case EncodingType::Raw:
    default:
        if (!fallbackRaw.isEmpty()) {
            return fallbackRaw;
        }
        return input.toUtf8();
    }
}

QString toHexDump(const QByteArray &data, int bytesPerLine) {
    if (data.isEmpty()) {
        return QString();
    }

    QString result;
    QTextStream out(&result);

    const int total = data.size();
    for (int i = 0; i < total; i += bytesPerLine) {
        // Offset
        out << QString("%1: ").arg(i, 8, 16, QChar('0')).toUpper();

        // Hex bytes
        const int chunk = std::min(bytesPerLine, total - i);
        for (int j = 0; j < bytesPerLine; ++j) {
            if (j < chunk) {
                uint8_t byte = static_cast<uint8_t>(data.at(i + j));
                out << QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
            } else {
                out << "   ";
            }
            if (j == 7) {
                out << " ";
            }
        }

        out << " |";
        // ASCII representation
        for (int j = 0; j < chunk; ++j) {
            uint8_t byte = static_cast<uint8_t>(data.at(i + j));
            if (byte >= 32 && byte <= 126) {
                out << static_cast<char>(byte);
            } else {
                out << '.';
            }
        }
        out << "|\n";
    }

    return result;
}

QString toPrintablePreview(const QByteArray &data, int maxLen) {
    int len = static_cast<int>(std::min<qsizetype>(data.size(), maxLen));
    QString preview;
    preview.reserve(len);

    for (int i = 0; i < len; ++i) {
        uint8_t b = static_cast<uint8_t>(data.at(i));
        if (b >= 32 && b <= 126) {
            preview.append(QChar(b));
        } else if (b == '\t' || b == '\n' || b == '\r') {
            preview.append(QChar(' '));
        } else {
            preview.append(QChar('.'));
        }
    }

    if (data.size() > maxLen) {
        preview.append(QStringLiteral("..."));
    }

    return preview;
}

QString toEscapedRepr(const QByteArray &data, int maxLen) {
    int len = static_cast<int>(std::min<qsizetype>(data.size(), maxLen));
    QString result = QStringLiteral("b\"");
    result.reserve(len * 4 + 4);

    for (int i = 0; i < len; ++i) {
        uint8_t b = static_cast<uint8_t>(data.at(i));
        if (b == '\\') {
            result.append(QStringLiteral("\\\\"));
        } else if (b == '\"') {
            result.append(QStringLiteral("\\\""));
        } else if (b == '\n') {
            result.append(QStringLiteral("\\n"));
        } else if (b == '\r') {
            result.append(QStringLiteral("\\r"));
        } else if (b == '\t') {
            result.append(QStringLiteral("\\t"));
        } else if (b >= 32 && b <= 126) {
            result.append(QChar(b));
        } else {
            result.append(QString("\\x%1").arg(b, 2, 16, QChar('0')));
        }
    }

    result.append(QChar('\"'));
    if (data.size() > maxLen) {
        result.append(QStringLiteral("..."));
    }
    return result;
}

QString encodingName(EncodingType type) {
    switch (type) {
    case EncodingType::Hex: return QStringLiteral("Hex");
    case EncodingType::Base64: return QStringLiteral("Base64");
    case EncodingType::Raw: return QStringLiteral("Raw");
    case EncodingType::Auto:
    default: return QStringLiteral("Auto");
    }
}

} // namespace EncodingUtils
