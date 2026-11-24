#include "EncodingUtils.h"
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <cstdint>

namespace EncodingUtils {

QString cleanInput(const QString &input) {
    QString cleaned = input.trimmed();
    cleaned.remove(QChar('\r'));
    cleaned.remove(QChar('\n'));
    cleaned.remove(QChar(' '));
    cleaned.remove(QChar('\t'));
    return cleaned;
}

QByteArray decodeBase32(const QString &input) {
    QString cleaned = cleanInput(input).toUpper();
    if (cleaned.isEmpty()) {
        return QByteArray();
    }

    QByteArray out;
    out.reserve((cleaned.length() * 5) / 8);

    uint32_t buffer = 0;
    int bitsLeft = 0;
    bool paddingStarted = false;

    for (const QChar &ch : cleaned) {
        char c = ch.toLatin1();
        if (c == '=') {
            paddingStarted = true;
            continue;
        }
        if (paddingStarted) {
            return QByteArray(); // Invalid data after padding
        }

        int val = -1;
        if (c >= 'A' && c <= 'Z') {
            val = c - 'A';
        } else if (c >= '2' && c <= '7') {
            val = 26 + (c - '2');
        } else {
            return QByteArray(); // Invalid Base32 character
        }

        buffer = (buffer << 5) | static_cast<uint32_t>(val);
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            bitsLeft -= 8;
            out.append(static_cast<char>((buffer >> bitsLeft) & 0xFF));
        }
    }

    return out;
}

static QByteArray decodeAscii85Impl(const QString &str) {
    QByteArray out;
    uint32_t tuple = 0;
    int count = 0;

    for (const QChar &ch : str) {
        char c = ch.toLatin1();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        if (c == 'z') {
            if (count != 0) {
                return QByteArray(); // 'z' cannot appear inside a 5-char group
            }
            out.append("\x00\x00\x00\x00", 4);
            continue;
        }
        if (c < '!' || c > 'u') {
            return QByteArray(); // Out of ASCII85 range
        }

        tuple = tuple * 85U + static_cast<uint32_t>(c - '!');
        count++;
        if (count == 5) {
            out.append(static_cast<char>((tuple >> 24) & 0xFF));
            out.append(static_cast<char>((tuple >> 16) & 0xFF));
            out.append(static_cast<char>((tuple >> 8) & 0xFF));
            out.append(static_cast<char>(tuple & 0xFF));
            tuple = 0;
            count = 0;
        }
    }

    if (count == 1) {
        return QByteArray(); // Single trailing char is invalid in ASCII85
    }
    if (count > 1) {
        for (int i = count; i < 5; ++i) {
            tuple = tuple * 85U + 84U; // Pad with 'u' (84)
        }
        for (int i = 0; i < count - 1; ++i) {
            out.append(static_cast<char>((tuple >> (24 - i * 8)) & 0xFF));
        }
    }

    return out;
}

static QByteArray decodeRfc1924Base85Impl(const QString &str) {
    static const char *B85_ALPHABET =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~";

    int lut[256];
    std::fill(std::begin(lut), std::end(lut), -1);
    for (int i = 0; i < 85; ++i) {
        lut[static_cast<uint8_t>(B85_ALPHABET[i])] = i;
    }

    QByteArray out;
    uint32_t tuple = 0;
    int count = 0;

    for (const QChar &ch : str) {
        uint8_t c = static_cast<uint8_t>(ch.toLatin1());
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        int val = lut[c];
        if (val < 0) {
            return QByteArray();
        }
        tuple = tuple * 85U + static_cast<uint32_t>(val);
        count++;
        if (count == 5) {
            out.append(static_cast<char>((tuple >> 24) & 0xFF));
            out.append(static_cast<char>((tuple >> 16) & 0xFF));
            out.append(static_cast<char>((tuple >> 8) & 0xFF));
            out.append(static_cast<char>(tuple & 0xFF));
            tuple = 0;
            count = 0;
        }
    }

    if (count == 1) {
        return QByteArray();
    }
    if (count > 1) {
        for (int i = count; i < 5; ++i) {
            tuple = tuple * 85U + 84U;
        }
        for (int i = 0; i < count - 1; ++i) {
            out.append(static_cast<char>((tuple >> (24 - i * 8)) & 0xFF));
        }
    }

    return out;
}

QByteArray decodeBase85(const QString &input) {
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return QByteArray();
    }

    // Explicit Adobe ASCII85 delimiters <~ ... ~>
    if (trimmed.startsWith(QStringLiteral("<~")) && trimmed.endsWith(QStringLiteral("~>")) && trimmed.length() >= 4) {
        return decodeAscii85Impl(trimmed.mid(2, trimmed.length() - 4));
    }

    // Check if characters fit Adobe ASCII85 ('!'..'u' or 'z') vs RFC 1924
    bool fitsAscii85 = true;
    for (const QChar &ch : trimmed) {
        char c = ch.toLatin1();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 'z') {
            continue;
        }
        if (c < '!' || c > 'u') {
            fitsAscii85 = false;
            break;
        }
    }

    if (fitsAscii85) {
        QByteArray res = decodeAscii85Impl(trimmed);
        if (!res.isEmpty()) {
            return res;
        }
    }

    return decodeRfc1924Base85Impl(trimmed);
}

QByteArray decodeUrl(const QString &input) {
    return QByteArray::fromPercentEncoding(input.trimmed().toUtf8());
}

QByteArray reverseBytes(const QByteArray &data) {
    QByteArray rev;
    rev.resize(data.size());
    std::reverse_copy(data.constBegin(), data.constEnd(), rev.begin());
    return rev;
}

EncodingType detectEncoding(const QString &input) {
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return EncodingType::Raw;
    }

    // 1. Check Adobe ASCII85 explicit wrapper <~ ... ~>
    if (trimmed.startsWith(QStringLiteral("<~")) && trimmed.endsWith(QStringLiteral("~>")) && trimmed.length() >= 4) {
        return EncodingType::Base85;
    }

    // 2. Check URL percent-encoding (%XX)
    static const QRegularExpression urlRegex(QStringLiteral("%[0-9A-Fa-f]{2}"));
    if (urlRegex.match(trimmed).hasMatch()) {
        return EncodingType::Url;
    }

    QString cleaned = cleanInput(input);
    if (cleaned.isEmpty()) {
        return EncodingType::Raw;
    }

    // Handle 0x prefix if present
    QString hexCandidate = cleaned;
    if (hexCandidate.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        hexCandidate = hexCandidate.mid(2);
    }

    // 3. Check HEX (strictly before Base64/Base32, e.g. "deadbeef")
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

    // 4. Check BASE32 (single-case A-Z2-7=, length multiple of 8, contains 2-7 or '=')
    if (cleaned.length() >= 8 && (cleaned.length() % 8 == 0)) {
        bool allUpperB32 = true;
        bool hasB32Marker = false;
        for (const QChar &ch : cleaned) {
            char c = ch.toLatin1();
            if (c >= '2' && c <= '7') {
                hasB32Marker = true;
            } else if (c == '=') {
                hasB32Marker = true;
            } else if (!(c >= 'A' && c <= 'Z')) {
                allUpperB32 = false;
                break;
            }
        }
        if (allUpperB32 && hasB32Marker && !decodeBase32(cleaned).isEmpty()) {
            return EncodingType::Base32;
        }
    }

    // 5. Check BASE64
    if (cleaned.length() >= 4 && (cleaned.length() % 4 == 0)) {
        bool validB64Chars = true;
        int equalCount = 0;
        int len = cleaned.length();

        for (int i = 0; i < len; ++i) {
            char c = cleaned.at(i).toLatin1();
            if (c == '=') {
                equalCount++;
                if (i < len - 2) {
                    validB64Chars = false;
                    break;
                }
            } else if (equalCount > 0) {
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

        if (validB64Chars) {
            QByteArray decoded = QByteArray::fromBase64(cleaned.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded.isEmpty()) {
                return EncodingType::Base64;
            }
        }
    }

    return EncodingType::Raw;
}

QByteArray decodeInput(const QString &input, EncodingType type, const QByteArray &fallbackRaw, bool reverse) {
    if (type == EncodingType::Auto) {
        type = detectEncoding(input);
    }

    QString cleaned = cleanInput(input);
    QByteArray result;

    switch (type) {
    case EncodingType::Hex: {
        QString hexStr = cleaned;
        if (hexStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            hexStr = hexStr.mid(2);
        }
        result = QByteArray::fromHex(hexStr.toLatin1());
        break;
    }
    case EncodingType::Base64:
        result = QByteArray::fromBase64(cleaned.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        break;
    case EncodingType::Base32:
        result = decodeBase32(input);
        break;
    case EncodingType::Base85:
        result = decodeBase85(input);
        break;
    case EncodingType::Url:
        result = decodeUrl(input);
        break;
    case EncodingType::Raw:
    default:
        result = !fallbackRaw.isEmpty() ? fallbackRaw : input.toUtf8();
        break;
    }

    if (reverse && !result.isEmpty()) {
        return reverseBytes(result);
    }
    return result;
}

QString toHexDump(const QByteArray &data, int bytesPerLine) {
    if (data.isEmpty()) {
        return QString();
    }

    QString result;
    QTextStream out(&result);

    const int total = data.size();
    for (int i = 0; i < total; i += bytesPerLine) {
        out << QString("%1: ").arg(i, 8, 16, QChar('0')).toUpper();

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
    case EncodingType::Hex:    return QStringLiteral("Hex");
    case EncodingType::Base64: return QStringLiteral("Base64");
    case EncodingType::Base32: return QStringLiteral("Base32");
    case EncodingType::Base85: return QStringLiteral("Base85");
    case EncodingType::Url:    return QStringLiteral("URL");
    case EncodingType::Raw:    return QStringLiteral("Raw");
    case EncodingType::Auto:
    default:                   return QStringLiteral("Auto");
    }
}

} // namespace EncodingUtils
