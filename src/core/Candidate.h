#pragma once

#include <QByteArray>
#include <QString>
#include <QMetaType>
#include <cstdint>

/**
 * @brief Represents a scored XOR decryption candidate.
 */
struct Candidate {
    int rank = 0;              ///< Position in sorted results (1-based)
    QByteArray key;           ///< Key bytes (1 to N bytes)
    double score = 0.0;       ///< Calculated score (higher is better)
    QByteArray plaintext;     ///< Decrypted plaintext
    QString preview;          ///< Human-readable preview with '.' for non-printable chars
    bool containsFlag = false;///< True if markers like FLAG, CTF, or flag{ are found
};

Q_DECLARE_METATYPE(Candidate)

/**
 * @brief Lightweight candidate representation for high-speed min-heap filtering.
 * Avoids allocating memory for plaintext/previews in hot loops.
 */
struct FastCandidate {
    uint32_t keyInt = 0;      ///< Key encoded as integer (up to 4 bytes)
    uint8_t keyLen = 1;       ///< Length of key in bytes
    double score = 0.0;       ///< Computed score

    bool operator>(const FastCandidate &other) const {
        return score > other.score; // Min-heap compares using '>' so top is smallest
    }
};
