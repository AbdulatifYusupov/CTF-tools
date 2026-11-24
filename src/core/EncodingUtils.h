#pragma once

#include <QString>
#include <QByteArray>

enum class EncodingType {
    Auto,
    Hex,
    Base64,
    Raw
};

namespace EncodingUtils {

/**
 * @brief Clean whitespace and line endings from input.
 */
QString cleanInput(const QString &input);

/**
 * @brief Automatically detects whether the input string is Hex, Base64, or Raw.
 * Note: Hex has priority over Base64 (e.g. "deadbeef" is recognized as Hex).
 */
EncodingType detectEncoding(const QString &input);

/**
 * @brief Decodes the given string into raw bytes according to the specified encoding.
 * If fallbackRaw is non-empty and encoding is Raw, fallbackRaw will be used.
 */
QByteArray decodeInput(const QString &input, EncodingType type, const QByteArray &fallbackRaw = QByteArray());

/**
 * @brief Converts binary data into a formatted hex dump (offset, hex, ASCII).
 */
QString toHexDump(const QByteArray &data, int bytesPerLine = 16);

/**
 * @brief Converts binary data to a printable preview string, replacing non-printables with '.'.
 */
QString toPrintablePreview(const QByteArray &data, int maxLen = 256);

/**
 * @brief Converts binary data into a Python/C-like escaped representation (e.g., b"\xde\xad...").
 */
QString toEscapedRepr(const QByteArray &data, int maxLen = 1024);

/**
 * @brief Returns human-readable name of encoding type.
 */
QString encodingName(EncodingType type);

} // namespace EncodingUtils
