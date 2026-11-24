#include "SettingsDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QSettings>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Scoring & Application Settings"));
    resize(480, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Scoring weights group
    QGroupBox *scoringGroup = new QGroupBox(tr("Scoring Engine Weights"), this);
    QFormLayout *formLayout = new QFormLayout(scoringGroup);

    m_printableWeight = new QDoubleSpinBox(this);
    m_printableWeight->setRange(0.0, 500.0);
    m_printableWeight->setSingleStep(5.0);
    formLayout->addRow(tr("Printable ASCII Weight:"), m_printableWeight);

    m_letterWeight = new QDoubleSpinBox(this);
    m_letterWeight->setRange(0.0, 500.0);
    m_letterWeight->setSingleStep(5.0);
    formLayout->addRow(tr("Letter Frequency Weight:"), m_letterWeight);

    m_nonPrintablePenalty = new QDoubleSpinBox(this);
    m_nonPrintablePenalty->setRange(0.0, 500.0);
    m_nonPrintablePenalty->setSingleStep(5.0);
    formLayout->addRow(tr("Non-Printable Penalty:"), m_nonPrintablePenalty);

    m_flagBonus = new QDoubleSpinBox(this);
    m_flagBonus->setRange(0.0, 1000.0);
    m_flagBonus->setSingleStep(25.0);
    formLayout->addRow(tr("FLAG / CTF Marker Bonus:"), m_flagBonus);

    m_bracketBonus = new QDoubleSpinBox(this);
    m_bracketBonus->setRange(0.0, 500.0);
    m_bracketBonus->setSingleStep(10.0);
    formLayout->addRow(tr("Enclosing Brackets {...} Bonus:"), m_bracketBonus);

    m_useChiSquared = new QCheckBox(tr("Enable Chi-Squared English Frequency Analysis"), this);
    formLayout->addRow(m_useChiSquared);

    m_chiSquaredWeight = new QDoubleSpinBox(this);
    m_chiSquaredWeight->setRange(0.0, 500.0);
    m_chiSquaredWeight->setSingleStep(5.0);
    formLayout->addRow(tr("Chi-Squared Weight:"), m_chiSquaredWeight);

    mainLayout->addWidget(scoringGroup);

    // Appearance Group
    QGroupBox *appearanceGroup = new QGroupBox(tr("Appearance"), this);
    QFormLayout *appLayout = new QFormLayout(appearanceGroup);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->addItem(tr("Dark (Default)"), QStringLiteral("dark"));
    m_themeCombo->addItem(tr("Light"), QStringLiteral("light"));
    appLayout->addRow(tr("UI Theme:"), m_themeCombo);

    mainLayout->addWidget(appearanceGroup);

    // Buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *resetBtn = new QPushButton(tr("Reset Defaults"), this);
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    btnLayout->addWidget(resetBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(buttonBox);
    mainLayout->addLayout(btnLayout);

    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetDefaults);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_useChiSquared, &QCheckBox::toggled, m_chiSquaredWeight, &QDoubleSpinBox::setEnabled);

    loadFromSettings();
}

void SettingsDialog::loadFromSettings() {
    QSettings settings;
    ScorerSettings s;
    s.load(settings);

    m_printableWeight->setValue(s.printableWeight);
    m_letterWeight->setValue(s.letterWeight);
    m_nonPrintablePenalty->setValue(s.nonPrintablePenalty);
    m_flagBonus->setValue(s.flagMarkerBonus);
    m_bracketBonus->setValue(s.bracketBonus);
    m_useChiSquared->setChecked(s.useChiSquared);
    m_chiSquaredWeight->setValue(s.chiSquaredWeight);
    m_chiSquaredWeight->setEnabled(s.useChiSquared);

    QString theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
    int idx = m_themeCombo->findData(theme);
    if (idx >= 0) {
        m_themeCombo->setCurrentIndex(idx);
    }
}

void SettingsDialog::resetDefaults() {
    ScorerSettings s;
    m_printableWeight->setValue(s.printableWeight);
    m_letterWeight->setValue(s.letterWeight);
    m_nonPrintablePenalty->setValue(s.nonPrintablePenalty);
    m_flagBonus->setValue(s.flagMarkerBonus);
    m_bracketBonus->setValue(s.bracketBonus);
    m_useChiSquared->setChecked(s.useChiSquared);
    m_chiSquaredWeight->setValue(s.chiSquaredWeight);
    m_chiSquaredWeight->setEnabled(s.useChiSquared);

    int idx = m_themeCombo->findData(QStringLiteral("dark"));
    if (idx >= 0) m_themeCombo->setCurrentIndex(idx);
}

ScorerSettings SettingsDialog::scorerSettings() const {
    ScorerSettings s;
    s.printableWeight = m_printableWeight->value();
    s.letterWeight = m_letterWeight->value();
    s.nonPrintablePenalty = m_nonPrintablePenalty->value();
    s.flagMarkerBonus = m_flagBonus->value();
    s.bracketBonus = m_bracketBonus->value();
    s.useChiSquared = m_useChiSquared->isChecked();
    s.chiSquaredWeight = m_chiSquaredWeight->value();
    return s;
}

QString SettingsDialog::selectedTheme() const {
    return m_themeCombo->currentData().toString();
}

void SettingsDialog::saveAndAccept() {
    QSettings settings;
    scorerSettings().save(settings);
    settings.setValue(QStringLiteral("ui/theme"), selectedTheme());
    accept();
}
