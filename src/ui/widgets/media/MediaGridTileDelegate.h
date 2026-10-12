#pragma once

#include <QFontMetrics>
#include <QPersistentModelIndex>

#include "FileNameEditDelegate.h"

class MediaViewerAnimationController;

// Paints a real decoded thumbnail (Architecture.md §14.62) + filename caption + two small corner
// metadata chips (size bottom-left, resolution-or-duration bottom-right) for Media Viewer mode's
// locked icon grid (FileBrowserView::DisplayMode::MediaViewerGrid) -- the only delegate installed
// in that mode. Layout math (icon box + one caption line, dashed selection outline) is copied from
// FileIconDelegate rather than inherited, since the two still differ in what they paint.
//
// Inherits inline-rename editing from FileNameEditDelegate (Architecture.md §14.75); only the
// editor's on-screen geometry needs overriding here, to match the name-caption rect paint()
// computes below -- same pattern FileIconDelegate/FileTileDelegate already use for Normal mode.
//
// Hover-scrub state (Architecture.md §14.64): FileBrowserView drives setHoveredPreview() from its
// own viewport mouse tracking (this delegate has no mouse-event handling of its own -- painting
// only). m_hoveredIndex/m_hoveredPreviewIndex are read back by FileBrowserView via the accessors
// below to know what to repaint when hover moves off a cell.
//
// `animationController` (Architecture.md §14.83) may be null (constructed for every
// FileBrowserView instance regardless of DisplayMode, but only ever non-null in
// MediaViewerGrid) -- paint() guards every use of it accordingly.
class MediaGridTileDelegate : public FileNameEditDelegate
{
    Q_OBJECT

public:
    explicit MediaGridTileDelegate(MediaViewerAnimationController* animationController, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

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

    // Computes the name-caption sub-rect within a plain cell rect, same reasoning as
    // thumbnailRectForCellRect() above -- shared by nameRectFor()'s paint()-facing wrapper, the
    // click-to-rename hit-test (FileBrowserView::mediaViewerNameRectFor), and
    // updateEditorGeometry().
    static QRect nameRectForCellRect(const QRect& cellRect, const QSize& iconSize, const QFontMetrics& fontMetrics);

private:
    static QRect nameRectFor(const QStyleOptionViewItem& option, const QModelIndex& index);
    static QRect thumbnailRectFor(const QStyleOptionViewItem& option);

    QPersistentModelIndex m_hoveredIndex;
    int m_hoveredPreviewIndex = -1;
    MediaViewerAnimationController* m_animationController = nullptr;
};
