#pragma once

#include <QFontMetrics>

#include "FileNameEditDelegate.h"

// Media Viewer mode's "Filmstrip" view (Architecture.md §14.69): one full-width row per item,
// showing every decoded preview frame (MediaPreviewFrames::kCount of them, Architecture.md
// §14.64) side-by-side with a thin divider between each, name caption left-aligned underneath
// (Architecture.md §14.75). An image (or a video not yet fully decoded) paints its single image
// stretched across the whole row instead. Installed as m_listView's delegate only while
// FileBrowserView::setMediaViewerViewMode(Filmstrip) is active -- no hover-scrub state of its own,
// since every frame is already visible at once.
//
// Inherits inline-rename editing from FileNameEditDelegate (Architecture.md §14.75); only the
// editor's on-screen geometry needs overriding here, to match the caption rect paint() computes
// below.
class MediaFilmstripDelegate : public FileNameEditDelegate
{
    Q_OBJECT

public:
    explicit MediaFilmstripDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Computes the caption sub-rect within a plain cell rect (no QStyleOptionViewItem needed), so
    // FileBrowserView::mediaViewerNameRectFor's click-hit-test can reuse the exact same layout this
    // delegate paints with -- same precedent as MediaGridTileDelegate::thumbnailRectForCellRect.
    static QRect captionRectForCellRect(const QRect& cellRect, const QFontMetrics& fontMetrics);
};
