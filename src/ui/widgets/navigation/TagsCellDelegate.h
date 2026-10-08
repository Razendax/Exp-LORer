#pragma once

#include <QStyledItemDelegate>

// Item delegate for FileListModel::TagsColumn (Architecture.md §14.x): paints one small rounded
// "pill" per tag (background = Tag::hexColor(), contrasting text) left-to-right, clipping extras
// with a trailing "+N" pill if they don't fit the column width. Paints directly with QPainter
// rather than instantiating real TagChipWidget child widgets per cell -- those are fine for the
// tag panel's small fixed chip lists, but too expensive per-row in a QTreeView.
class TagsCellDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit TagsCellDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
