#pragma once

#include <QStyledItemDelegate>

// Paints icon + two lines of text (name, then type/size) for Tiles view mode
// (Architecture.md §2.3.1) — Qt has no built-in tile layout, so FileBrowserView swaps this in as
// the QListView's item delegate only while Tiles is the active view mode.
class FileTileDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FileTileDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
