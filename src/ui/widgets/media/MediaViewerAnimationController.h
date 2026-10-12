#pragma once

#include <filesystem>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPersistentModelIndex>

#include "MediaViewerPreviewMode.h"

class QListView;
class QMovie;

// Owns the real QMovie lifecycles for Media Viewer mode's Static/Animate/Autoplay GIF preview
// (Architecture.md §14.83). One instance per Media Viewer grid (FileBrowserView), kept as a
// shared object rather than duplicated per-delegate since animation state must survive switching
// between the Thumbnails and Details delegates (both address the same QPersistentModelIndex), and
// since starting/stopping movies reacts to hover/scroll events that originate in FileBrowserView,
// not in paint().
class MediaViewerAnimationController : public QObject
{
    Q_OBJECT

public:
    explicit MediaViewerAnimationController(QListView* view, QObject* parent = nullptr);

    // Switching into Static stops and discards every tracked movie immediately (followed by a
    // viewport repaint) so visible rows snap back to their static thumbnail right away.
    void setMode(MediaViewerPreviewMode mode);

    // Stops and discards every tracked movie without touching the stored mode, so switching back
    // to the delegate that reads frameIfAnimating() resumes exactly where the mode selector left
    // it. Used by FileBrowserView when swapping to MediaFilmstripDelegate, which never calls
    // frameIfAnimating() at all (Architecture.md §14.69) and so would otherwise leave stale movies
    // decoding frames in the background for nothing.
    void stopAllForDelegateSwap();

    // AnimateOnHover's hovered row, or an invalid index when nothing (or a non-animated row) is
    // hovered. No-op repaint side effects -- the caller issues the actual repaint (comparing against
    // hoveredIndex() beforehand to know what changed), same posture as
    // MediaGridTileDelegate::setHoveredPreview.
    void setHoveredIndex(const QModelIndex& index);
    QModelIndex hoveredIndex() const { return m_hoveredIndex; }

    // Called only for rows MediaExtensions::isAnimatedImageExtension() is true for. Returns a null
    // QImage when `index` shouldn't currently render as animated (wrong mode, or AnimateOnHover and
    // not the hovered row) -- the caller should paint its normal cached static thumbnail instead.
    // Otherwise lazily starts (or reuses) a QMovie for `filePath` and returns its current frame.
    QImage frameIfAnimating(const QPersistentModelIndex& index, const std::filesystem::path& filePath);

private:
    void pruneOffscreen();
    void clear();

    QListView* m_view;
    MediaViewerPreviewMode m_mode = MediaViewerPreviewMode::Static;
    QPersistentModelIndex m_hoveredIndex;
    QHash<QPersistentModelIndex, QMovie*> m_activeMovies;
};
