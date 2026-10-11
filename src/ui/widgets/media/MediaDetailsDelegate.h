#pragma once

#include <QFont>
#include <QPersistentModelIndex>

#include "FileNameEditDelegate.h"
#include "Tag.h"

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
//
// Inherits inline-rename editing from FileNameEditDelegate (Architecture.md §14.75); only the
// editor's on-screen geometry needs overriding here, to match the name rect paint() computes.
class MediaDetailsDelegate : public FileNameEditDelegate
{
    Q_OBJECT

public:
    explicit MediaDetailsDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Hit-tests the tags row (Architecture.md §14.72): on a left-button release over a tag pill,
    // emits tagActivated and consumes the event; otherwise falls through to the base
    // implementation.
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                      const QModelIndex& index) override;

    void setHoveredPreview(const QModelIndex& index, int previewIndex);
    QModelIndex hoveredIndex() const { return m_hoveredIndex; }
    int hoveredPreviewIndex() const { return m_hoveredPreviewIndex; }

    // Small fixed-size square (derived from `iconSize`) anchored at the row's left edge,
    // vertically centered -- mirrors MediaGridTileDelegate::thumbnailRectForCellRect's shape, so
    // FileBrowserView's hover-band math can reuse it the same way.
    static QRect thumbnailRectForCellRect(const QRect& cellRect, const QSize& iconSize);

    // Computes the name-line sub-rect within a plain cell rect, same reasoning as
    // thumbnailRectForCellRect() above -- shared by computeNameRect()'s paint()-facing wrapper,
    // the click-to-rename hit-test (FileBrowserView::mediaViewerNameRectFor), and
    // updateEditorGeometry(). Takes the base QFont (not QFontMetrics) since the bold/+1-point name
    // font is derived from it internally.
    static QRect nameRectForCellRect(const QRect& cellRect, const QSize& iconSize, const QFont& baseFont);

signals:
    void tagActivated(const QModelIndex& index, Tag::Id tagId, Qt::KeyboardModifiers modifiers);

private:
    static QRect thumbnailRectFor(const QStyleOptionViewItem& option);

    // Factored out of paint() (Architecture.md §14.72) so editorEvent()'s hit-test uses the exact
    // same geometry paint() draws the tag pills at -- the geometry doesn't depend on hasThumbnail,
    // only what text gets drawn inside it does.
    QRect computeTagsRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    // Thin option/index-based wrapper over nameRectForCellRect(), used by both paint() and
    // computeTagsRect().
    QRect computeNameRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    QPersistentModelIndex m_hoveredIndex;
    int m_hoveredPreviewIndex = -1;
};
