#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QPair>
#include <QAtomicInt>
#include <memory>

#include "Candidate.h"
#include "Scorer.h"

class XorEngine : public QObject {
    Q_OBJECT
public:
    explicit XorEngine(QObject *parent = nullptr);
    ~XorEngine() override;

    void setScorer(const Scorer &scorer);
    const Scorer& scorer() const { return m_scorer; }

    /**
     * @brief Estimate probable key lengths using normalized Hamming distance & Index of Coincidence.
     * @param ciphertext Raw encrypted data.
     * @param maxKeyLen Max key length to test (default 32).
     * @return List of pairs: <KeyLength, Score/Confidence (higher is better)>, sorted best first.
     */
    static QVector<QPair<int, double>> estimateKeyLengths(const QByteArray &ciphertext, int maxKeyLen = 32);

public slots:
    /**
     * @brief Single-byte XOR brute force (0x00 to 0xFF).
     */
    void runSingleByte(const QByteArray &ciphertext, int topN = 256);

    /**
     * @brief Multi-byte exhaustive brute force (parallelized, key length 2 or 3).
     */
    void runMultiByte(const QByteArray &ciphertext, int keyLength, int topN = 200);

    /**
     * @brief Smart mode for multi-byte XOR (transposition & frequency analysis per column).
     */
    void runSmartMultiByte(const QByteArray &ciphertext, int keyLength, int topN = 200);

    /**
     * @brief Known-plaintext attack with optional sliding crib dragging.
     */
    void runKnownPlaintext(const QByteArray &ciphertext, const QByteArray &crib, int assumedKeyLen = 0, bool cribDrag = true, int topN = 200);

    /**
     * @brief Byte-wise ADD/SUB ((b - k) & 0xFF) and Caesar ROT-N / ROT-47 brute force.
     */
    void runAddSubRot(const QByteArray &ciphertext, int topN = 200);

    /**
     * @brief Bitwise NOT, ROL/ROR (1..7 bits), and combinations with single-byte XOR.
     */
    void runBitwiseShiftNot(const QByteArray &ciphertext, int topN = 200);

    /**
     * @brief Combo mode: Single-byte XOR combined with ADD/SUB (65,536x2) and/or ROL/ROR.
     */
    void runComboXorAddRol(const QByteArray &ciphertext, bool includeXorAdd = true, bool includeXorRol = true, int topN = 200);

    /**
     * @brief Request graceful cancellation of ongoing brute-force.
     */
    void cancel();

signals:
    void progress(int percentage, quint64 keysChecked, quint64 totalKeys);
    void finished(QVector<Candidate> results);
    void error(const QString &errorMessage);
    void statusMessage(const QString &message);

private:
    Candidate materializeCandidate(uint32_t keyInt, uint8_t keyLen, const QByteArray &ciphertext) const;
    Candidate materializeCandidateFromBytes(const QByteArray &key, const QByteArray &ciphertext, const QString &keyDesc = QString()) const;
    Candidate materializeTransformCandidate(const FastCandidate &fc, const QByteArray &ciphertext) const;

    Scorer m_scorer;
    QAtomicInt m_cancelRequested{0};
};
