#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QSettings>
#include <cstdint>
#include <cstddef>
#include <array>

struct ScorerSettings {
    double printableWeight = 50.0;
    double letterWeight = 30.0;
    double nonPrintablePenalty = 20.0;
    double flagMarkerBonus = 150.0;
    double bracketBonus = 40.0;
    bool useChiSquared = false;
    double chiSquaredWeight = 25.0;

    void load(QSettings &settings);
    void save(QSettings &settings) const;
};

class Scorer {
public:
    explicit Scorer(const ScorerSettings &settings = ScorerSettings());

    void setSettings(const ScorerSettings &settings);
    const ScorerSettings& settings() const { return m_settings; }

    /**
     * @brief High-speed zero-allocation scoring over raw byte array.
     * Uses precomputed lookup table. Ideal for inner loops of brute-force.
     */
    double calculateFastScore(const uint8_t *data, size_t length) const;

    /**
     * @brief Detailed full scoring for candidates that pass the fast threshold / top-N candidates.
     * Checks for flag markers, brackets, and optionally chi-squared English frequency analysis.
     * Also detects if candidate contains flag markers.
     */
    double calculateDetailedScore(const QByteArray &plaintext, bool &outContainsFlag) const;

    /**
     * @brief Computes English chi-squared statistic (lower is better match, converted to score).
     */
    static double calculateChiSquared(const uint8_t *data, size_t length);

private:
    void rebuildLookupTable();

    ScorerSettings m_settings;
    std::array<double, 256> m_charScoreTable{};
};
