#pragma once

#include <QDialog>
#include "../core/Scorer.h"

class QDoubleSpinBox;
class QCheckBox;
class QComboBox;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    ScorerSettings scorerSettings() const;
    QString selectedTheme() const;

private slots:
    void resetDefaults();
    void saveAndAccept();

private:
    void loadFromSettings();

    QDoubleSpinBox *m_printableWeight = nullptr;
    QDoubleSpinBox *m_letterWeight = nullptr;
    QDoubleSpinBox *m_nonPrintablePenalty = nullptr;
    QDoubleSpinBox *m_flagBonus = nullptr;
    QDoubleSpinBox *m_bracketBonus = nullptr;
    QCheckBox *m_useChiSquared = nullptr;
    QDoubleSpinBox *m_chiSquaredWeight = nullptr;
    QComboBox *m_themeCombo = nullptr;
};
