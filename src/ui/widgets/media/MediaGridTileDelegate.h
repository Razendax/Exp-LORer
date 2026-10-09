#pragma once

#include <QStyledItemDelegate>

// Paints a real decoded thumbnail (Architecture.md §14.62) + filename caption + two small corner
// metadata chips (size bottom-left, resolution-or-duration bottom-right) for Media Viewer mode's
// locked icon grid (FileBrowserView::DisplayMode::MediaViewerGrid) -- the only delegate installed
// in that mode. Plain QStyledItemDelegate, not FileIconDelegate/FileNameEditDelegate: F2/rename is
// already disabled in MediaViewerGrid mode, so there's no reason to carry that plumbing here.
// Layout math (icon box + one caption line, dashed selection outline) is copied from
// FileIconDelegate rather than inherited, for the same reason.
class MediaGridTileDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MediaGridTileDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    static QRect nameRectFor(const QStyleOptionViewItem& option, const QModelIndex& index);
    static QRect thumbnailRectFor(const QStyleOptionViewItem& option);
};
