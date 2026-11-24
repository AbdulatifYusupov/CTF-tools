#include "Scorer.h"
#include <cmath>
#include <algorithm>
#include <cctype>

// Standard English letter frequencies (A-Z)
static const double ENGLISH_FREQUENCIES[26] = {
    0.08167, 0.01492, 0.02782, 0.04253, 0.12702, 0.02228, 0.02015, // A-G
    0.06094, 0.06966, 0.00153, 0.00772, 0.04025, 0.02406, 0.06749, // H-N
    0.07507, 0.01929, 0.00095, 0.05987, 0.06327, 0.09056, 0.02758, // O-U
    0.00978, 0.02360, 0.00150, 0.01974, 0.00074                    // V-Z
};

void ScorerSettings::load(QSettings &settings) {
    printableWeight = settings.value(QStringLiteral("scoring/printableWeight"), 50.0).toDouble();
    letterWeight = settings.value(QStringLiteral("scoring/letterWeight"), 30.0).toDouble();
    nonPrintablePenalty = settings.value(QStringLiteral("scoring/nonPrintablePenalty"), 20.0).toDouble();
    flagMarkerBonus = settings.value(QStringLiteral("scoring/flagMarkerBonus"), 150.0).toDouble();
    bracketBonus = settings.value(QStringLiteral("scoring/bracketBonus"), 40.0).toDouble();
    useChiSquared = settings.value(QStringLiteral("scoring/useChiSquared"), false).toBool();
    chiSquaredWeight = settings.value(QStringLiteral("scoring/chiSquaredWeight"), 25.0).toDouble();
}

void ScorerSettings::save(QSettings &settings) const {
    settings.setValue(QStringLiteral("scoring/printableWeight"), printableWeight);
    settings.setValue(QStringLiteral("scoring/letterWeight"), letterWeight);
    settings.setValue(QStringLiteral("scoring/nonPrintablePenalty"), nonPrintablePenalty);
    settings.setValue(QStringLiteral("scoring/flagMarkerBonus"), flagMarkerBonus);
    settings.setValue(QStringLiteral("scoring/bracketBonus"), bracketBonus);
    settings.setValue(QStringLiteral("scoring/useChiSquared"), useChiSquared);
    settings.setValue(QStringLiteral("scoring/chiSquaredWeight"), chiSquaredWeight);
}

Scorer::Scorer(const ScorerSettings &settings)
    : m_settings(settings)
{
    rebuildLookupTable();
}

void Scorer::setSettings(const ScorerSettings &settings) {
    m_settings = settings;
    rebuildLookupTable();
}

void Scorer::rebuildLookupTable() {
    for (int i = 0; i < 256; ++i) {
        double score = 0.0;
        if (i >= 32 && i <= 126) {
            // Printable ASCII
            score += m_settings.printableWeight;

            // Letters A-Z, a-z
            if (i >= 'a' && i <= 'z') {
                score += m_settings.letterWeight * (1.0 + ENGLISH_FREQUENCIES[i - 'a'] * 5.0);
            } else if (i >= 'A' && i <= 'Z') {
                score += (m_settings.letterWeight * 0.8) * (1.0 + ENGLISH_FREQUENCIES[i - 'A'] * 5.0);
            } else if (i >= '0' && i <= '9') {
                score += m_settings.letterWeight * 0.4;
            } else if (i == '_' || i == '-' || i == '{' || i == '}' || i == ' ') {
                score += m_settings.letterWeight * 0.6;
            }
        } else if (i == '\t' || i == '\n' || i == '\r') {
            score += m_settings.printableWeight * 0.5;
        } else {
            // Non-printable penalty
            score -= m_settings.nonPrintablePenalty;
        }
        m_charScoreTable[i] = score;
    }
}

double Scorer::calculateFastScore(const uint8_t *data, size_t length) const {
    if (length == 0 || data == nullptr) {
        return -1000.0;
    }

    double totalScore = 0.0;
    const uint8_t *ptr = data;
    const uint8_t *end = data + length;

    while (ptr < end) {
        totalScore += m_charScoreTable[*ptr++];
    }

    return totalScore / static_cast<double>(length);
}

double Scorer::calculateDetailedScore(const QByteArray &plaintext, bool &outContainsFlag) const {
    const size_t len = static_cast<size_t>(plaintext.size());
    if (len == 0) {
        outContainsFlag = false;
        return -1000.0;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t*>(plaintext.constData());
    double score = calculateFastScore(data, len);

    outContainsFlag = false;
    QByteArray lower = plaintext.toLower();

    // Check for standard flag markers
    if (lower.contains("flag{") || lower.contains("flag-") || lower.contains("ctf{") || lower.contains("ctf-")) {
        score += m_settings.flagMarkerBonus * 1.5;
        outContainsFlag = true;
    } else if (plaintext.contains("FLAG") || lower.contains("flag") || plaintext.contains("CTF") || lower.contains("ctf")) {
        score += m_settings.flagMarkerBonus;
        outContainsFlag = true;
    }

    // Check for bracket enclosing patterns, e.g. {...}
    int openBracket = plaintext.indexOf('{');
    if (openBracket >= 0) {
        int closeBracket = plaintext.indexOf('}', openBracket + 1);
        if (closeBracket > openBracket) {
            score += m_settings.bracketBonus;
        }
    }

    // Optional Chi-squared analysis
    if (m_settings.useChiSquared) {
        double chi = calculateChiSquared(data, len);
        // Typical english chi-squared is low (e.g. 0.5 to 5.0).
        // Larger chi means unnatural frequency.
        if (chi < 50.0) {
            score += m_settings.chiSquaredWeight * (1.0 - (chi / 50.0));
        } else {
            score -= m_settings.chiSquaredWeight * 0.5;
        }
    }

    return score;
}

double Scorer::calculateChiSquared(const uint8_t *data, size_t length) {
    if (length == 0 || data == nullptr) {
        return 9999.0;
    }

    uint32_t counts[26] = {0};
    uint32_t letterCount = 0;

    for (size_t i = 0; i < length; ++i) {
        uint8_t c = data[i];
        if (c >= 'a' && c <= 'z') {
            counts[c - 'a']++;
            letterCount++;
        } else if (c >= 'A' && c <= 'Z') {
            counts[c - 'A']++;
            letterCount++;
        }
    }

    if (letterCount < 5) {
        return 9999.0;
    }

    double chiSq = 0.0;
    for (int i = 0; i < 26; ++i) {
        double expected = letterCount * ENGLISH_FREQUENCIES[i];
        double diff = counts[i] - expected;
        chiSq += (diff * diff) / (expected > 0.0001 ? expected : 0.0001);
    }

    return chiSq;
}
