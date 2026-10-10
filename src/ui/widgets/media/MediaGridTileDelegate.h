#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

// Paints a real decoded thumbnail (Architecture.md §14.62) + filename caption + two small corner
// metadata chips (size bottom-left, resolution-or-duration bottom-right) for Media Viewer mode's
// locked icon grid (FileBrowserView::DisplayMode::MediaViewerGrid) -- the only delegate installed
// in that mode. Plain QStyledItemDelegate, not FileIconDelegate/FileNameEditDelegate: F2/rename is
// already disabled in MediaViewerGrid mode, so there's no reason to carry that plumbing here.
// Layout math (icon box + one caption line, dashed selection outline) is copied from
// FileIconDelegate rather than inherited, for the same reason.
//
// Hover-scrub state (Architecture.md §14.64): FileBrowserView drives setHoveredPreview() from its
// own viewport mouse tracking (this delegate has no mouse-event handling of its own -- painting
// only). m_hoveredIndex/m_hoveredPreviewIndex are read back by FileBrowserView via the accessors
// below to know what to repaint when hover moves off a cell.
class MediaGridTileDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MediaGridTileDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Sets which of a video row's MediaPreviewFrames::kCount frames to paint instead of its
    // default poster frame. An invalid index clears hover entirely (no separate clear method
    // needed). No-op repaint side effects here -- the caller (FileBrowserView) is responsible for
    // issuing the actual QListView::update() calls.
    void setHoveredPreview(const QModelIndex& index, int previewIndex);
    QModelIndex hoveredIndex() const { return m_hoveredIndex; }
    int hoveredPreviewIndex() const { return m_hoveredPreviewIndex; }

    // Computes the thumbnail sub-rect within a plain cell rect (no QStyleOptionViewItem needed),
    // so FileBrowserView's hover-band math can reuse the exact same layout this delegate paints
    // with. thumbnailRectFor() below is now a one-line wrapper around this.
    static QRect thumbnailRectForCellRect(const QRect& cellRect, const QSize& iconSize);

private:
    static QRect nameRectFor(const QStyleOptionViewItem& option, const QModelIndex& index);
    static QRect thumbnailRectFor(const QStyleOptionViewItem& option);

    QPersistentModelIndex m_hoveredIndex;
    int m_hoveredPreviewIndex = -1;
};
