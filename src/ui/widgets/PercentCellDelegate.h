#pragma once

#include <QStyledItemDelegate>

// Item delegate for FileListModel::PercentColumn (Architecture.md §14.39): paints a blue-ish bar
// filled left-to-right in proportion to FileListModel::PercentRole, with the "NN%" label centered
// in the whole cell (fixed position, not chasing the fill edge). Paints directly with QPainter,
// same posture as TagsCellDelegate. Falls back to the stock delegate (blank cell) when
// PercentRole is invalid -- an unscanned directory, or a folder with no known total size yet.
class PercentCellDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit PercentCellDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
