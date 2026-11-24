#include "XorEngine.h"
#include "EncodingUtils.h"

#include <QtConcurrent>
#include <QFuture>
#include <QThreadPool>
#include <algorithm>
#include <vector>
#include <queue>
#include <cmath>

#if defined(__GNUC__) || defined(__clang__)
#define POPCOUNT(x) __builtin_popcount(x)
#else
static inline int POPCOUNT(unsigned int x) {
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    return (((x + (x >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}
#endif

XorEngine::XorEngine(QObject *parent)
    : QObject(parent)
{
}

XorEngine::~XorEngine() = default;

void XorEngine::setScorer(const Scorer &scorer) {
    m_scorer = scorer;
}

void XorEngine::cancel() {
    m_cancelRequested.storeRelaxed(1);
}

// --------------------------------------------------------------------------
// Key Length Estimation
// --------------------------------------------------------------------------

QVector<QPair<int, double>> XorEngine::estimateKeyLengths(const QByteArray &ciphertext, int maxKeyLen) {
    QVector<QPair<int, double>> results;
    const int len = ciphertext.size();
    if (len < 8) {
        return results;
    }

    maxKeyLen = std::min(maxKeyLen, len / 2);
    if (maxKeyLen < 2) {
        return results;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t*>(ciphertext.constData());

    for (int k = 2; k <= maxKeyLen; ++k) {
        // 1. Normalized Hamming distance across up to 4 consecutive blocks
        int blocks = std::min(len / k, 4);
        if (blocks < 2) {
            continue;
        }

        double totalDist = 0.0;
        int pairs = 0;

        for (int b1 = 0; b1 < blocks - 1; ++b1) {
            for (int b2 = b1 + 1; b2 < blocks; ++b2) {
                int dist = 0;
                for (int i = 0; i < k; ++i) {
                    uint8_t diff = data[b1 * k + i] ^ data[b2 * k + i];
                    dist += POPCOUNT(diff);
                }
                totalDist += static_cast<double>(dist) / static_cast<double>(k);
                pairs++;
            }
        }

        double avgNormDist = (pairs > 0) ? (totalDist / pairs) : 8.0;

        // 2. Index of Coincidence (IoC) across slices
        double totalIoc = 0.0;
        for (int col = 0; col < k; ++col) {
            uint32_t counts[256] = {0};
            int colLen = 0;
            for (int i = col; i < len; i += k) {
                counts[data[i]]++;
                colLen++;
            }
            if (colLen > 1) {
                double sum = 0.0;
                for (int i = 0; i < 256; ++i) {
                    sum += counts[i] * (counts[i] - 1);
                }
                totalIoc += sum / (static_cast<double>(colLen) * (colLen - 1));
            }
        }
        double avgIoc = totalIoc / k;

        // Combined score: lower Hamming distance is better (normal ~ 2.5-3.5), higher IoC is better (English ~ 0.067).
        // Score = (8.0 - avgNormDist) * 10.0 + (avgIoc * 500.0)
        double score = (8.0 - avgNormDist) * 10.0 + (avgIoc * 500.0);
        results.append(qMakePair(k, score));
    }

    std::sort(results.begin(), results.end(), [](const QPair<int, double> &a, const QPair<int, double> &b) {
        return a.second > b.second; // Higher score first
    });

    return results;
}

// --------------------------------------------------------------------------
// Single-Byte XOR
// --------------------------------------------------------------------------

void XorEngine::runSingleByte(const QByteArray &ciphertext, int topN) {
    m_cancelRequested.storeRelaxed(0);

    if (ciphertext.isEmpty()) {
        emit error(QStringLiteral("Ciphertext is empty."));
        return;
    }

    emit statusMessage(QStringLiteral("Running single-byte brute force (256 keys)..."));
    emit progress(0, 0, 256);

    const int len = ciphertext.size();
    const uint8_t *data = reinterpret_cast<const uint8_t*>(ciphertext.constData());

    std::priority_queue<FastCandidate, std::vector<FastCandidate>, std::greater<FastCandidate>> minHeap;

    for (int k = 0; k < 256; ++k) {
        if (m_cancelRequested.loadRelaxed()) {
            emit statusMessage(QStringLiteral("Cancelled by user."));
            return;
        }

        // Direct in-place fast scoring without allocation
        double scoreSum = 0.0;
        for (int i = 0; i < len; ++i) {
            uint8_t dec = data[i] ^ static_cast<uint8_t>(k);
            // We use Scorer's fast lookup table via calculateFastScore logic
            // Since m_scorer is accessible, we can score chunks or bytes
            scoreSum += m_scorer.calculateFastScore(&dec, 1);
        }
        double avgScore = scoreSum / len;

        FastCandidate cand;
        cand.keyInt = static_cast<uint32_t>(k);
        cand.keyLen = 1;
        cand.score = avgScore;

        minHeap.push(cand);
        if (static_cast<int>(minHeap.size()) > topN) {
            minHeap.pop();
        }
    }

    emit progress(100, 256, 256);

    // Materialize surviving candidates
    QVector<Candidate> results;
    results.reserve(minHeap.size());

    while (!minHeap.empty()) {
        FastCandidate fc = minHeap.top();
        minHeap.pop();
        results.append(materializeCandidate(fc.keyInt, fc.keyLen, ciphertext));
    }

    // Sort descending by final detailed score
    std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });

    for (int i = 0; i < results.size(); ++i) {
        results[i].rank = i + 1;
    }

    emit statusMessage(QStringLiteral("Single-byte search completed. Found %1 candidates.").arg(results.size()));
    emit finished(results);
}

// --------------------------------------------------------------------------
// Multi-Byte Exhaustive Search
// --------------------------------------------------------------------------

void XorEngine::runMultiByte(const QByteArray &ciphertext, int keyLength, int topN) {
    m_cancelRequested.storeRelaxed(0);

    if (ciphertext.isEmpty()) {
        emit error(QStringLiteral("Ciphertext is empty."));
        return;
    }
    if (keyLength < 2 || keyLength > 3) {
        emit error(QStringLiteral("Exhaustive multi-byte mode supports key length 2 or 3."));
        return;
    }

    const quint64 totalKeys = (keyLength == 2) ? 65536ULL : 16777216ULL;
    emit statusMessage(QStringLiteral("Running parallel multi-byte search (%1 keys, length %2)...")
                           .arg(totalKeys).arg(keyLength));
    emit progress(0, 0, totalKeys);

    const int len = ciphertext.size();
    const uint8_t *data = reinterpret_cast<const uint8_t*>(ciphertext.constData());

    std::atomic<quint64> keysChecked{0};

    // Partition work by outer byte k0 (0..255)
    QVector<int> outerBytes(256);
    std::iota(outerBytes.begin(), outerBytes.end(), 0);

    using LocalHeap = std::priority_queue<FastCandidate, std::vector<FastCandidate>, std::greater<FastCandidate>>;

    auto workerSlice = [this, data, len, keyLength, topN, totalKeys, &keysChecked](int k0) -> LocalHeap {
        LocalHeap heap;
        if (m_cancelRequested.loadRelaxed()) {
            return heap;
        }

        if (keyLength == 2) {
            for (int k1 = 0; k1 < 256; ++k1) {
                uint8_t keyBytes[2] = {static_cast<uint8_t>(k0), static_cast<uint8_t>(k1)};

                double scoreSum = 0.0;
                for (int i = 0; i < len; ++i) {
                    uint8_t dec = data[i] ^ keyBytes[i & 1];
                    scoreSum += m_scorer.calculateFastScore(&dec, 1);
                }
                double avgScore = scoreSum / len;

                FastCandidate cand;
                cand.keyInt = (static_cast<uint32_t>(k0) << 8) | static_cast<uint32_t>(k1);
                cand.keyLen = 2;
                cand.score = avgScore;

                heap.push(cand);
                if (static_cast<int>(heap.size()) > topN) {
                    heap.pop();
                }
            }
            quint64 current = keysChecked.fetch_add(256, std::memory_order_relaxed) + 256;
            if ((current & 0x0FFF) == 0) {
                int pct = static_cast<int>((current * 100) / totalKeys);
                emit progress(pct, current, totalKeys);
            }
        } else if (keyLength == 3) {
            for (int k1 = 0; k1 < 256; ++k1) {
                if (m_cancelRequested.loadRelaxed()) {
                    break;
                }
                for (int k2 = 0; k2 < 256; ++k2) {
                    uint8_t keyBytes[3] = {static_cast<uint8_t>(k0), static_cast<uint8_t>(k1), static_cast<uint8_t>(k2)};

                    double scoreSum = 0.0;
                    for (int i = 0; i < len; ++i) {
                        uint8_t dec = data[i] ^ keyBytes[i % 3];
                        scoreSum += m_scorer.calculateFastScore(&dec, 1);
                    }
                    double avgScore = scoreSum / len;

                    // Only push if heap has room or score exceeds current minimum
                    if (static_cast<int>(heap.size()) < topN || avgScore > heap.top().score) {
                        FastCandidate cand;
                        cand.keyInt = (static_cast<uint32_t>(k0) << 16) | (static_cast<uint32_t>(k1) << 8) | static_cast<uint32_t>(k2);
                        cand.keyLen = 3;
                        cand.score = avgScore;

                        heap.push(cand);
                        if (static_cast<int>(heap.size()) > topN) {
                            heap.pop();
                        }
                    }
                }

                // Check cancel and report progress every 16384 keys
                quint64 current = keysChecked.fetch_add(256, std::memory_order_relaxed) + 256;
                if ((current & 0x3FFF) == 0) {
                    if (m_cancelRequested.loadRelaxed()) {
                        break;
                    }
                    int pct = static_cast<int>((current * 100) / totalKeys);
                    emit progress(pct, current, totalKeys);
                }
            }
        }
        return heap;
    };

    // Run parallel across threads
    QList<LocalHeap> allHeaps = QtConcurrent::blockingMapped(outerBytes, workerSlice);

    if (m_cancelRequested.loadRelaxed()) {
        emit statusMessage(QStringLiteral("Search cancelled."));
        return;
    }

    emit statusMessage(QStringLiteral("Merging candidates and calculating final scores..."));

    // Merge heaps
    LocalHeap globalHeap;
    for (LocalHeap &lh : allHeaps) {
        while (!lh.empty()) {
            globalHeap.push(lh.top());
            lh.pop();
            if (static_cast<int>(globalHeap.size()) > topN) {
                globalHeap.pop();
            }
        }
    }

    emit progress(100, totalKeys, totalKeys);

    // Materialize
    QVector<Candidate> results;
    results.reserve(globalHeap.size());
    while (!globalHeap.empty()) {
        FastCandidate fc = globalHeap.top();
        globalHeap.pop();
        results.append(materializeCandidate(fc.keyInt, fc.keyLen, ciphertext));
    }

    std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });

    for (int i = 0; i < results.size(); ++i) {
        results[i].rank = i + 1;
    }

    emit statusMessage(QStringLiteral("Search completed. Found %1 top candidates.").arg(results.size()));
    emit finished(results);
}

// --------------------------------------------------------------------------
// Smart Multi-Byte Mode (Transposition & Frequency Analysis per Column)
// --------------------------------------------------------------------------

void XorEngine::runSmartMultiByte(const QByteArray &ciphertext, int keyLength, int topN) {
    m_cancelRequested.storeRelaxed(0);

    if (ciphertext.isEmpty()) {
        emit error(QStringLiteral("Ciphertext is empty."));
        return;
    }
    if (keyLength < 1) {
        emit error(QStringLiteral("Invalid key length."));
        return;
    }

    emit statusMessage(QStringLiteral("Running Smart Mode frequency analysis for key length %1...").arg(keyLength));
    emit progress(0, 0, keyLength * 256);

    const int len = ciphertext.size();
    const uint8_t *data = reinterpret_cast<const uint8_t*>(ciphertext.constData());

    // For each column, find top candidate bytes
    QVector<QVector<QPair<uint8_t, double>>> columnCandidates(keyLength);

    for (int col = 0; col < keyLength; ++col) {
        QVector<QPair<uint8_t, double>> byteScores;
        byteScores.reserve(256);

        // Slice for this column
        for (int b = 0; b < 256; ++b) {
            double colScoreSum = 0.0;
            int count = 0;
            for (int i = col; i < len; i += keyLength) {
                uint8_t dec = data[i] ^ static_cast<uint8_t>(b);
                colScoreSum += m_scorer.calculateFastScore(&dec, 1);
                count++;
            }
            double avg = (count > 0) ? (colScoreSum / count) : 0.0;
            byteScores.append(qMakePair(static_cast<uint8_t>(b), avg));
        }

        std::sort(byteScores.begin(), byteScores.end(), [](const QPair<uint8_t, double> &a, const QPair<uint8_t, double> &b) {
            return a.second > b.second;
        });

        columnCandidates[col] = byteScores;
        emit progress(static_cast<int>((col + 1) * 100 / keyLength), (col + 1) * 256, keyLength * 256);
    }

    // Assemble top keys. Primary candidate: best byte for each column
    QVector<Candidate> results;
    QByteArray bestKey;
    bestKey.resize(keyLength);
    for (int col = 0; col < keyLength; ++col) {
        bestKey[col] = static_cast<char>(columnCandidates[col].first().first);
    }
    results.append(materializeCandidateFromBytes(bestKey, ciphertext));

    // Also combine top 2-3 variants if keyLength <= 8 to give rich candidates
    if (keyLength <= 6) {
        // Generate permutations of top 2 bytes for each column
        int variations = 1 << keyLength;
        for (int mask = 1; mask < std::min(variations, topN); ++mask) {
            QByteArray altKey = bestKey;
            for (int col = 0; col < keyLength; ++col) {
                if ((mask >> col) & 1) {
                    if (columnCandidates[col].size() > 1) {
                        altKey[col] = static_cast<char>(columnCandidates[col][1].first);
                    }
                }
            }
            results.append(materializeCandidateFromBytes(altKey, ciphertext));
        }
    } else {
        // For larger keys, vary each column individually to 2nd and 3rd best
        for (int col = 0; col < keyLength; ++col) {
            for (int r = 1; r < std::min<qsizetype>(4, columnCandidates[col].size()); ++r) {
                QByteArray altKey = bestKey;
                altKey[col] = static_cast<char>(columnCandidates[col][r].first);
                results.append(materializeCandidateFromBytes(altKey, ciphertext));
            }
        }
    }

    std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });

    if (results.size() > topN) {
        results.resize(topN);
    }

    for (int i = 0; i < results.size(); ++i) {
        results[i].rank = i + 1;
    }

    emit statusMessage(QStringLiteral("Smart analysis completed. Found %1 candidates.").arg(results.size()));
    emit finished(results);
}

// --------------------------------------------------------------------------
// Known-Plaintext & Crib Dragging
// --------------------------------------------------------------------------

void XorEngine::runKnownPlaintext(const QByteArray &ciphertext, const QByteArray &crib, int assumedKeyLen, bool cribDrag, int topN) {
    m_cancelRequested.storeRelaxed(0);

    if (ciphertext.isEmpty()) {
        emit error(QStringLiteral("Ciphertext is empty."));
        return;
    }
    if (crib.isEmpty()) {
        emit error(QStringLiteral("Crib (known plaintext) cannot be empty."));
        return;
    }
    if (crib.size() > ciphertext.size()) {
        emit error(QStringLiteral("Crib is longer than ciphertext."));
        return;
    }

    emit statusMessage(QStringLiteral("Running known-plaintext analysis with crib '%1'...").arg(QString::fromUtf8(crib)));

    const int cipherLen = ciphertext.size();
    const int cribLen = crib.size();
    const int maxOffset = cribDrag ? (cipherLen - cribLen) : 0;

    QVector<Candidate> results;
    QSet<QByteArray> testedKeys;

    for (int offset = 0; offset <= maxOffset; ++offset) {
        if (m_cancelRequested.loadRelaxed()) {
            break;
        }

        // Key length candidates to test for this offset
        QVector<int> keyLensToTry;
        if (assumedKeyLen > 0) {
            keyLensToTry.append(assumedKeyLen);
        } else {
            // Try crib length, plus sub-factors / lengths 1..cribLen (up to 16)
            for (int k = 1; k <= std::min(cribLen, 16); ++k) {
                keyLensToTry.append(k);
            }
        }

        for (int kLen : keyLensToTry) {
            // Construct key candidate aligned with offset
            QByteArray candKey;
            candKey.resize(kLen);
            QVector<bool> byteSet(kLen, false);
            bool consistent = true;

            for (int j = 0; j < cribLen; ++j) {
                int keyIdx = (offset + j) % kLen;
                uint8_t derivedByte = static_cast<uint8_t>(ciphertext.at(offset + j) ^ crib.at(j));
                if (byteSet[keyIdx]) {
                    if (static_cast<uint8_t>(candKey.at(keyIdx)) != derivedByte) {
                        consistent = false;
                        break;
                    }
                } else {
                    candKey[keyIdx] = static_cast<char>(derivedByte);
                    byteSet[keyIdx] = true;
                }
            }

            if (!consistent) {
                continue;
            }

            // If some bytes of the key were not covered by the crib, fill with 0 or skip
            bool fullyDetermined = true;
            for (int b = 0; b < kLen; ++b) {
                if (!byteSet[b]) {
                    fullyDetermined = false;
                    break;
                }
            }

            if (fullyDetermined && !testedKeys.contains(candKey)) {
                testedKeys.insert(candKey);
                results.append(materializeCandidateFromBytes(candKey, ciphertext));
            }
        }
    }

    std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });

    if (results.size() > topN) {
        results.resize(topN);
    }

    for (int i = 0; i < results.size(); ++i) {
        results[i].rank = i + 1;
    }

    emit statusMessage(QStringLiteral("Known-plaintext search completed. Found %1 unique candidate keys.").arg(results.size()));
    emit finished(results);
}

// --------------------------------------------------------------------------
// Transform Helpers (Shared by ADD/ROT, Bitwise, and Combo modes)
// --------------------------------------------------------------------------

static inline uint8_t rol8(uint8_t v, int r) {
    r &= 7;
    if (r == 0) return v;
    return static_cast<uint8_t>((v << r) | (v >> (8 - r)));
}

static inline uint8_t ror8(uint8_t v, int r) {
    r &= 7;
    if (r == 0) return v;
    return static_cast<uint8_t>((v >> r) | (v << (8 - r)));
}

static inline uint8_t applyTransformByte(uint8_t b, TransformOp op, uint32_t keyInt) {
    switch (op) {
    case TransformOp::ByteSub:
        return static_cast<uint8_t>((b - (keyInt & 0xFF)) & 0xFF);
    case TransformOp::CaesarRot: {
        int n = static_cast<int>(keyInt & 0xFF);
        if (b >= 'a' && b <= 'z') {
            return static_cast<uint8_t>('a' + ((b - 'a' + 26 - (n % 26)) % 26));
        }
        if (b >= 'A' && b <= 'Z') {
            return static_cast<uint8_t>('A' + ((b - 'A' + 26 - (n % 26)) % 26));
        }
        return b;
    }
    case TransformOp::Rot47:
        if (b >= 33 && b <= 126) {
            return static_cast<uint8_t>(33 + ((b - 33 + 47) % 94));
        }
        return b;
    case TransformOp::BitNot:
        return static_cast<uint8_t>(~b);
    case TransformOp::BitRol:
        return rol8(b, static_cast<int>(keyInt & 0x07));
    case TransformOp::BitRor:
        return ror8(b, static_cast<int>(keyInt & 0x07));
    case TransformOp::NotXor:
        return static_cast<uint8_t>((~b) ^ (keyInt & 0xFF));
    case TransformOp::RolXor:
        return static_cast<uint8_t>(rol8(b, static_cast<int>((keyInt >> 8) & 0x07)) ^ (keyInt & 0xFF));
    case TransformOp::RorXor:
        return static_cast<uint8_t>(ror8(b, static_cast<int>((keyInt >> 8) & 0x07)) ^ (keyInt & 0xFF));
    case TransformOp::XorRol:
        return rol8(static_cast<uint8_t>(b ^ (keyInt & 0xFF)), static_cast<int>((keyInt >> 8) & 0x07));
    case TransformOp::XorRor:
        return ror8(static_cast<uint8_t>(b ^ (keyInt & 0xFF)), static_cast<int>((keyInt >> 8) & 0x07));
    case TransformOp::XorSub:
        return static_cast<uint8_t>(((b ^ ((keyInt >> 8) & 0xFF)) - (keyInt & 0xFF)) & 0xFF);
    case TransformOp::SubXor:
        return static_cast<uint8_t>(((b - (keyInt & 0xFF)) & 0xFF) ^ ((keyInt >> 8) & 0xFF));
    case TransformOp::Xor:
    default:
        return static_cast<uint8_t>(b ^ (keyInt & 0xFF));
    }
}

static QString formatTransformDesc(TransformOp op, uint32_t keyInt) {
    const uint32_t k = keyInt & 0xFF;
    const uint32_t hi = (keyInt >> 8) & 0xFF;
    switch (op) {
    case TransformOp::ByteSub:
        return QStringLiteral("SUB 0x%1 (ADD 0x%2)")
            .arg(QString::number(k, 16).rightJustified(2, '0').toUpper())
            .arg(QString::number((256 - k) & 0xFF, 16).rightJustified(2, '0').toUpper());
    case TransformOp::CaesarRot:
        return QStringLiteral("ROT-%1").arg(k);
    case TransformOp::Rot47:
        return QStringLiteral("ROT-47");
    case TransformOp::BitNot:
        return QStringLiteral("NOT (~b)");
    case TransformOp::BitRol:
        return QStringLiteral("ROL(%1)").arg(k & 7);
    case TransformOp::BitRor:
        return QStringLiteral("ROR(%1)").arg(k & 7);
    case TransformOp::NotXor:
        return QStringLiteral("(~b) ^ 0x%1").arg(QString::number(k, 16).rightJustified(2, '0').toUpper());
    case TransformOp::RolXor:
        return QStringLiteral("ROL(b,%1) ^ 0x%2").arg(hi & 7).arg(QString::number(k, 16).rightJustified(2, '0').toUpper());
    case TransformOp::RorXor:
        return QStringLiteral("ROR(b,%1) ^ 0x%2").arg(hi & 7).arg(QString::number(k, 16).rightJustified(2, '0').toUpper());
    case TransformOp::XorRol:
        return QStringLiteral("ROL(b^0x%1,%2)").arg(QString::number(k, 16).rightJustified(2, '0').toUpper()).arg(hi & 7);
    case TransformOp::XorRor:
        return QStringLiteral("ROR(b^0x%1,%2)").arg(QString::number(k, 16).rightJustified(2, '0').toUpper()).arg(hi & 7);
    case TransformOp::XorSub:
        return QStringLiteral("(b^0x%1)-0x%2")
            .arg(QString::number(hi, 16).rightJustified(2, '0').toUpper())
            .arg(QString::number(k, 16).rightJustified(2, '0').toUpper());
    case TransformOp::SubXor:
        return QStringLiteral("(b-0x%2)^0x%1")
            .arg(QString::number(hi, 16).rightJustified(2, '0').toUpper())
            .arg(QString::number(k, 16).rightJustified(2, '0').toUpper());
    case TransformOp::Xor:
    default:
        return QString();
    }
}

// --------------------------------------------------------------------------
// Byte ADD/SUB & ROT-N / Caesar
// --------------------------------------------------------------------------

void XorEngine::runAddSubRot(const QByteArray &ciphertext, int topN) {
    m_cancelRequested.storeRelaxed(0);
    if (ciphertext.isEmpty()) {
        emit error(QStringLiteral("Ciphertext is empty."));
        return;
    }

    const quint64 totalKeys = 256 + 25 + 1;
    emit statusMessage(QStringLiteral("Running Byte ADD/SUB & ROT-N / ROT-47..."));
    emit progress(0, 0, totalKeys);

    const int len = ciphertext.size();
    const uint8_t *data = reinterpret_cast<const uint8_t*>(ciphertext.constData());

    std::priority_queue<FastCandidate, std::vector<FastCandidate>, std::greater<FastCandidate>> minHeap;

    auto evalOp = [&](TransformOp op, uint32_t keyInt) {
        double scoreSum = 0.0;
        for (int i = 0; i < len; ++i) {
            uint8_t dec = applyTransformByte(data[i], op, keyInt);
            scoreSum += m_scorer.calculateFastScore(&dec, 1);
        }
        FastCandidate fc;
        fc.op = op;
        fc.keyInt = keyInt;
        fc.keyLen = 1;
        fc.score = scoreSum / len;
        minHeap.push(fc);
        if (static_cast<int>(minHeap.size()) > topN) {
            minHeap.pop();
        }
    };

    // 1. Byte ADD/SUB (0..255)
    for (uint32_t k = 0; k < 256; ++k) {
        if (m_cancelRequested.loadRelaxed()) return;
        evalOp(TransformOp::ByteSub, k);
    }

    // 2. Caesar ROT-1..ROT-25
    for (uint32_t n = 1; n <= 25; ++n) {
        if (m_cancelRequested.loadRelaxed()) return;
        evalOp(TransformOp::CaesarRot, n);
    }

    // 3. ROT-47
    evalOp(TransformOp::Rot47, 47);

    emit progress(100, totalKeys, totalKeys);

    QVector<Candidate> results;
    results.reserve(minHeap.size());
    while (!minHeap.empty()) {
        FastCandidate fc = minHeap.top();
        minHeap.pop();
        results.append(materializeTransformCandidate(fc, ciphertext));
    }

    std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });
    for (int i = 0; i < results.size(); ++i) {
        results[i].rank = i + 1;
    }

    emit statusMessage(QStringLiteral("ADD/SUB & ROT search completed. Found %1 candidates.").arg(results.size()));
    emit finished(results);
}

// --------------------------------------------------------------------------
// Bitwise Shifts (ROL / ROR 1-7) + NOT
// --------------------------------------------------------------------------

void XorEngine::runBitwiseShiftNot(const QByteArray &ciphertext, int topN) {
    m_cancelRequested.storeRelaxed(0);
    if (ciphertext.isEmpty()) {
        emit error(QStringLiteral("Ciphertext is empty."));
        return;
    }

    const quint64 totalKeys = 1 + 7 + 7 + 256 + (256 * 7 * 4);
    emit statusMessage(QStringLiteral("Running Bitwise NOT & ROL/ROR (1-7) + XOR..."));
    emit progress(0, 0, totalKeys);

    const int len = ciphertext.size();
    const uint8_t *data = reinterpret_cast<const uint8_t*>(ciphertext.constData());

    std::priority_queue<FastCandidate, std::vector<FastCandidate>, std::greater<FastCandidate>> minHeap;
    quint64 checked = 0;

    auto evalOp = [&](TransformOp op, uint32_t keyInt) {
        double scoreSum = 0.0;
        for (int i = 0; i < len; ++i) {
            uint8_t dec = applyTransformByte(data[i], op, keyInt);
            scoreSum += m_scorer.calculateFastScore(&dec, 1);
        }
        double avg = scoreSum / len;
        if (static_cast<int>(minHeap.size()) < topN || avg > minHeap.top().score) {
            FastCandidate fc;
            fc.op = op;
            fc.keyInt = keyInt;
            fc.keyLen = 1;
            fc.score = avg;
            minHeap.push(fc);
            if (static_cast<int>(minHeap.size()) > topN) {
                minHeap.pop();
            }
        }
        checked++;
    };

    // Pure NOT, ROL(1..7), ROR(1..7)
    evalOp(TransformOp::BitNot, 0);
    for (uint32_t r = 1; r <= 7; ++r) {
        evalOp(TransformOp::BitRol, r);
        evalOp(TransformOp::BitRor, r);
    }

    // NOT ^ k
    for (uint32_t k = 0; k < 256; ++k) {
        if (m_cancelRequested.loadRelaxed()) return;
        evalOp(TransformOp::NotXor, k);
    }

    // ROL/ROR combined with XOR in both orders
    for (uint32_t r = 1; r <= 7; ++r) {
        for (uint32_t k = 0; k < 256; ++k) {
            if ((checked & 0x3FF) == 0 && m_cancelRequested.loadRelaxed()) return;
            uint32_t packed = (r << 8) | k;
            evalOp(TransformOp::RolXor, packed);
            evalOp(TransformOp::RorXor, packed);
            evalOp(TransformOp::XorRol, packed);
            evalOp(TransformOp::XorRor, packed);
        }
    }

    emit progress(100, totalKeys, totalKeys);

    QVector<Candidate> results;
    results.reserve(minHeap.size());
    while (!minHeap.empty()) {
        FastCandidate fc = minHeap.top();
        minHeap.pop();
        results.append(materializeTransformCandidate(fc, ciphertext));
    }

    std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });
    for (int i = 0; i < results.size(); ++i) {
        results[i].rank = i + 1;
    }

    emit statusMessage(QStringLiteral("Bitwise NOT/ROL/ROR search completed. Checked %1 combinations.").arg(checked));
    emit finished(results);
}

// --------------------------------------------------------------------------
// Combo Mode: Single-Byte XOR + ADD/SUB & ROL/ROR
// --------------------------------------------------------------------------

void XorEngine::runComboXorAddRol(const QByteArray &ciphertext, bool includeXorAdd, bool includeXorRol, int topN) {
    m_cancelRequested.storeRelaxed(0);
    if (ciphertext.isEmpty()) {
        emit error(QStringLiteral("Ciphertext is empty."));
        return;
    }
    if (!includeXorAdd && !includeXorRol) {
        emit error(QStringLiteral("Select at least one combo family (XOR+ADD or XOR+ROL)."));
        return;
    }

    const quint64 rolKeys = includeXorRol ? (256ULL * 7ULL * 4ULL) : 0ULL;
    const quint64 addKeys = includeXorAdd ? (256ULL * 256ULL * 2ULL) : 0ULL;
    const quint64 totalKeys = rolKeys + addKeys;

    emit statusMessage(QStringLiteral("Running Combo XOR + ADD/ROL (%1 combinations)...").arg(totalKeys));
    emit progress(0, 0, totalKeys);

    const int len = ciphertext.size();
    const uint8_t *data = reinterpret_cast<const uint8_t*>(ciphertext.constData());
    std::atomic<quint64> keysChecked{0};

    QVector<int> outerBytes(256);
    std::iota(outerBytes.begin(), outerBytes.end(), 0);

    using LocalHeap = std::priority_queue<FastCandidate, std::vector<FastCandidate>, std::greater<FastCandidate>>;

    auto workerSlice = [&](int k1) -> LocalHeap {
        LocalHeap heap;
        if (m_cancelRequested.loadRelaxed()) return heap;

        auto pushCandidate = [&](TransformOp op, uint32_t keyInt) {
            double scoreSum = 0.0;
            for (int i = 0; i < len; ++i) {
                uint8_t dec = applyTransformByte(data[i], op, keyInt);
                scoreSum += m_scorer.calculateFastScore(&dec, 1);
            }
            double avg = scoreSum / len;
            if (static_cast<int>(heap.size()) < topN || avg > heap.top().score) {
                FastCandidate fc;
                fc.op = op;
                fc.keyInt = keyInt;
                fc.keyLen = 2;
                fc.score = avg;
                heap.push(fc);
                if (static_cast<int>(heap.size()) > topN) {
                    heap.pop();
                }
            }
        };

        if (includeXorRol) {
            for (uint32_t r = 1; r <= 7; ++r) {
                uint32_t packed = (r << 8) | static_cast<uint32_t>(k1);
                pushCandidate(TransformOp::RolXor, packed);
                pushCandidate(TransformOp::RorXor, packed);
                pushCandidate(TransformOp::XorRol, packed);
                pushCandidate(TransformOp::XorRor, packed);
            }
            keysChecked.fetch_add(28, std::memory_order_relaxed);
        }

        if (includeXorAdd) {
            for (int k2 = 0; k2 < 256; ++k2) {
                uint32_t packed = (static_cast<uint32_t>(k1) << 8) | static_cast<uint32_t>(k2);
                pushCandidate(TransformOp::XorSub, packed);
                pushCandidate(TransformOp::SubXor, packed);
            }
            quint64 current = keysChecked.fetch_add(512, std::memory_order_relaxed) + 512;
            if ((current & 0x3FFF) == 0) {
                int pct = static_cast<int>((current * 100) / totalKeys);
                emit progress(pct, current, totalKeys);
            }
        }

        return heap;
    };

    QList<LocalHeap> allHeaps = QtConcurrent::blockingMapped(outerBytes, workerSlice);

    if (m_cancelRequested.loadRelaxed()) {
        emit statusMessage(QStringLiteral("Combo search cancelled."));
        return;
    }

    LocalHeap globalHeap;
    for (LocalHeap &lh : allHeaps) {
        while (!lh.empty()) {
            globalHeap.push(lh.top());
            lh.pop();
            if (static_cast<int>(globalHeap.size()) > topN) {
                globalHeap.pop();
            }
        }
    }

    emit progress(100, totalKeys, totalKeys);

    QVector<Candidate> results;
    results.reserve(globalHeap.size());
    while (!globalHeap.empty()) {
        FastCandidate fc = globalHeap.top();
        globalHeap.pop();
        results.append(materializeTransformCandidate(fc, ciphertext));
    }

    std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b) {
        return a.score > b.score;
    });
    for (int i = 0; i < results.size(); ++i) {
        results[i].rank = i + 1;
    }

    emit statusMessage(QStringLiteral("Combo search completed (%1 combinations). Found %2 top candidates.")
                           .arg(totalKeys).arg(results.size()));
    emit finished(results);
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

Candidate XorEngine::materializeCandidate(uint32_t keyInt, uint8_t keyLen, const QByteArray &ciphertext) const {
    QByteArray key;
    key.resize(keyLen);
    for (int i = 0; i < keyLen; ++i) {
        // Big-endian unpacking matching key construction
        int shift = (keyLen - 1 - i) * 8;
        key[i] = static_cast<char>((keyInt >> shift) & 0xFF);
    }
    return materializeCandidateFromBytes(key, ciphertext);
}

Candidate XorEngine::materializeCandidateFromBytes(const QByteArray &key, const QByteArray &ciphertext, const QString &keyDesc) const {
    Candidate c;
    c.key = key;
    c.keyDesc = keyDesc;

    const int cipherLen = ciphertext.size();
    const int keyLen = key.size();
    c.plaintext.resize(cipherLen);

    const uint8_t *cData = reinterpret_cast<const uint8_t*>(ciphertext.constData());
    const uint8_t *kData = reinterpret_cast<const uint8_t*>(key.constData());
    uint8_t *pData = reinterpret_cast<uint8_t*>(c.plaintext.data());

    for (int i = 0; i < cipherLen; ++i) {
        pData[i] = cData[i] ^ kData[i % keyLen];
    }

    c.score = m_scorer.calculateDetailedScore(c.plaintext, c.containsFlag);
    c.preview = EncodingUtils::toPrintablePreview(c.plaintext);
    return c;
}

Candidate XorEngine::materializeTransformCandidate(const FastCandidate &fc, const QByteArray &ciphertext) const {
    Candidate c;
    c.key.append(static_cast<char>(fc.keyInt & 0xFF));
    if (fc.keyLen == 2) {
        c.key.prepend(static_cast<char>((fc.keyInt >> 8) & 0xFF));
    }
    c.keyDesc = formatTransformDesc(fc.op, fc.keyInt);

    const int cipherLen = ciphertext.size();
    c.plaintext.resize(cipherLen);
    const uint8_t *cData = reinterpret_cast<const uint8_t*>(ciphertext.constData());
    uint8_t *pData = reinterpret_cast<uint8_t*>(c.plaintext.data());

    for (int i = 0; i < cipherLen; ++i) {
        pData[i] = applyTransformByte(cData[i], fc.op, fc.keyInt);
    }

    c.score = m_scorer.calculateDetailedScore(c.plaintext, c.containsFlag);
    c.preview = EncodingUtils::toPrintablePreview(c.plaintext);
    return c;
}
