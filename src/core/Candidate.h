#pragma once

#include <QByteArray>
#include <QString>
#include <QMetaType>
#include <cstdint>

/**
 * @brief Transformation operation type for extended CTF brute-force modes.
 */
enum class TransformOp : uint8_t {
    Xor = 0,       ///< Standard repeating-key XOR
    ByteSub,       ///< (b - k) & 0xFF (covers ADD & SUB)
    CaesarRot,     ///< Letter rotation ROT-N (1..25)
    Rot47,         ///< Printable ASCII rotation ROT-47
    BitNot,        ///< ~b & 0xFF
    BitRol,        ///< ROL(b, r)
    BitRor,        ///< ROR(b, r)
    NotXor,        ///< (~b) ^ k
    RolXor,        ///< ROL(b, r) ^ k
    RorXor,        ///< ROR(b, r) ^ k
    XorRol,        ///< ROL(b ^ k, r)
    XorRor,        ///< ROR(b ^ k, r)
    XorSub,        ///< ((b ^ k1) - k2) & 0xFF
    SubXor         ///< ((b - k2) & 0xFF) ^ k1
};

/**
 * @brief Represents a scored decryption candidate.
 */
struct Candidate {
    int rank = 0;               ///< Position in sorted results (1-based)
    QByteArray key;             ///< Key bytes (1 to N bytes)
    QString keyDesc;            ///< Human-readable operation/key label (if non-empty, shown in Key column)
    double score = 0.0;         ///< Calculated score (higher is better)
    QByteArray plaintext;       ///< Decrypted plaintext
    QString preview;            ///< Human-readable preview with '.' for non-printable chars
    bool containsFlag = false;  ///< True if markers like FLAG, CTF, or flag{ are found
};

Q_DECLARE_METATYPE(Candidate)

/**
 * @brief Lightweight candidate representation for high-speed min-heap filtering.
 * Avoids allocating memory for plaintext/previews in hot loops.
 */
struct FastCandidate {
    uint32_t keyInt = 0;        ///< Key/params encoded as integer
    uint8_t keyLen = 1;         ///< Length of key in bytes (for XOR)
    TransformOp op = TransformOp::Xor;
    double score = 0.0;         ///< Computed score

    bool operator>(const FastCandidate &other) const {
        return score > other.score; // Min-heap compares using '>' so top is smallest
    }
};
