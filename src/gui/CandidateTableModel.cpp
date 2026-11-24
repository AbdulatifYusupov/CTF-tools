#include "CandidateTableModel.h"
#include <QColor>

CandidateTableModel::CandidateTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int CandidateTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_candidates.size();
}

int CandidateTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant CandidateTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_candidates.size()) {
        return QVariant();
    }

    const Candidate &c = m_candidates.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case ColRank:
            return c.rank; // Returns integer for correct numeric sort
        case ColKeyHex:
            return QString::fromLatin1(c.key.toHex().toUpper());
        case ColKeyAscii: {
            QString ascii;
            for (char b : c.key) {
                uint8_t ub = static_cast<uint8_t>(b);
                ascii.append((ub >= 32 && ub <= 126) ? QChar(ub) : QChar('.'));
            }
            return ascii;
        }
        case ColScore:
            return QString::number(c.score, 'f', 1);
        case ColFlag:
            return c.containsFlag ? tr("YES") : QString();
        case ColPreview:
            return c.preview;
        default:
            return QVariant();
        }
    }

    if (role == Qt::UserRole) {
        // Return raw types for proxy sorting if needed
        if (index.column() == ColRank) return c.rank;
        if (index.column() == ColScore) return c.score;
        if (index.column() == ColFlag) return c.containsFlag ? 1 : 0;
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColRank || index.column() == ColScore) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        if (index.column() == ColFlag) {
            return static_cast<int>(Qt::AlignCenter | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ForegroundRole && index.column() == ColFlag && c.containsFlag) {
        return QColor(80, 250, 123); // Bright green marker
    }

    return QVariant();
}

QVariant CandidateTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case ColRank:     return tr("Rank");
        case ColKeyHex:   return tr("Key (Hex)");
        case ColKeyAscii: return tr("Key (ASCII)");
        case ColScore:    return tr("Score");
        case ColFlag:     return tr("Flag");
        case ColPreview:  return tr("Preview");
        default: break;
        }
    }
    return QVariant();
}

void CandidateTableModel::setCandidates(const QVector<Candidate> &candidates) {
    beginResetModel();
    m_candidates = candidates;
    endResetModel();
}

const Candidate& CandidateTableModel::candidateAt(int row) const {
    return m_candidates.at(row);
}

void CandidateTableModel::clear() {
    beginResetModel();
    m_candidates.clear();
    endResetModel();
}
