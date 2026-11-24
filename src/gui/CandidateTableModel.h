#pragma once

#include <QAbstractTableModel>
#include <QVector>
#include "../core/Candidate.h"

class CandidateTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColRank = 0,
        ColKeyHex,
        ColKeyAscii,
        ColScore,
        ColFlag,
        ColPreview,
        ColumnCount
    };

    explicit CandidateTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setCandidates(const QVector<Candidate> &candidates);
    const Candidate& candidateAt(int row) const;
    void clear();

    const QVector<Candidate>& allCandidates() const { return m_candidates; }

private:
    QVector<Candidate> m_candidates;
};
