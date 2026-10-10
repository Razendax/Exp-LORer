#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

// Media Viewer mode's "Details" view (Architecture.md §14.69): one full-width row per item, a
// hover-scrubbable preview thumbnail on the left (same interaction as the Thumbnails grid,
// Architecture.md §14.64) and name/duration-or-size-or-resolution/tags on the right. Installed as
// m_listView's delegate only while FileBrowserView::setMediaViewerViewMode(Details) is active.
//
// Hover state mirrors MediaGridTileDelegate's shape exactly (same reasoning as that class: Qt's
// per-cell single-delegate model means two independently-laid-out regions of one cell can't share
// a literal delegate instance, so the API shape is mirrored instead and the actual pixel-painting
// code is shared via MediaThumbnailPainting). FileBrowserView drives setHoveredPreview() from its
// own viewport mouse tracking; m_hoveredIndex/m_hoveredPreviewIndex are read back by
// FileBrowserView to know what to repaint when hover moves off a cell.
class MediaDetailsDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MediaDetailsDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setHoveredPreview(const QModelIndex& index, int previewIndex);
    QModelIndex hoveredIndex() const { return m_hoveredIndex; }
    int hoveredPreviewIndex() const { return m_hoveredPreviewIndex; }

    // Small fixed-size square (derived from `iconSize`) anchored at the row's left edge,
    // vertically centered -- mirrors MediaGridTileDelegate::thumbnailRectForCellRect's shape, so
    // FileBrowserView's hover-band math can reuse it the same way.
    static QRect thumbnailRectForCellRect(const QRect& cellRect, const QSize& iconSize);

private:
    static QRect thumbnailRectFor(const QStyleOptionViewItem& option);

    QPersistentModelIndex m_hoveredIndex;
    int m_hoveredPreviewIndex = -1;
};
