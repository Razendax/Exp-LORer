#pragma once

#include <chrono>
#include <vector>

#include <QString>

class QPainter;
class QRect;
class QImage;
class QIcon;

// Shared pixel-painting routines for Media Viewer mode's thumbnail delegates (Architecture.md
// §14.62/§14.69) -- lifted out of MediaGridTileDelegate.cpp's anonymous namespace unchanged, now
// used by MediaGridTileDelegate, MediaFilmstripDelegate, and MediaDetailsDelegate (3 call sites).
// Unlike the small per-delegate layout-rect math this codebase deliberately keeps duplicated
// (§14.62's own comment), these are non-trivial image-scaling/formatting routines that earn a
// shared helper once a third call site needs them.
namespace MediaThumbnailPainting
{
    // Aspect-fit, letterboxed inside `rect` -- a real photo/poster-frame's own aspect ratio must be
    // preserved or it looks visibly distorted.
    void paintThumbnail(QPainter* painter, const QImage& image, const QRect& rect);

    // Placeholder-icon fallback while a real thumbnail hasn't arrived yet -- always stretches to
    // fill `rect`, fine for a square system icon.
    void paintPlaceholderIcon(QPainter* painter, const QIcon& icon, const QRect& rect);

    // Media Viewer Folder-Grouped Browsing's subfolder tile collage (Architecture.md §14.74):
    // divides `rect` into a 2x2 grid and calls paintThumbnail() per quadrant for each of `images`
    // (0-4 entries, already-decoded sample children in the current sort order); any quadrant
    // without a sample yet (images.size() < 4, or a sample still mid-decode) is painted as a
    // neutral placeholder square instead of left blank.
    void paintFolderPreviewCollage(QPainter* painter, const std::vector<QImage>& images, const QRect& rect);

    // Same mm:ss/hh:mm:ss formatting as MediaPreviewWidget::setPreview.
    QString durationLabel(std::chrono::milliseconds duration);
}
