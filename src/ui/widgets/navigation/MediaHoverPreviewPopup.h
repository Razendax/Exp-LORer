#pragma once

#include <filesystem>

#include <QPixmap>
#include <QWidget>

class QMovie;

// Frameless, non-interactive popup showing an image/video file's preview beside the mouse cursor
// while hovering its row in the normal file browser (Architecture.md §14.76/§14.79). Modeled on
// the frameless-popup precedent in AddressBarWidget/TagSearchEdit, but a static display rather
// than an interactive list: WA_TransparentForMouseEvents means it never steals hover/click from
// the view underneath it. Animated GIFs are the one exception to "static": showAnimatedGif()
// drives this popup from its own QMovie instead of a single decoded pixmap.
class MediaHoverPreviewPopup : public QWidget
{
    Q_OBJECT

public:
    explicit MediaHoverPreviewPopup(QWidget* parent = nullptr);

    // Scales `pixmap` to fit within a fixed max display size (aspect preserved), resizes to that
    // plus a small border, and positions itself beside `globalAnchorPos` -- to its right by
    // default, flipping to the left if it would overflow the anchor screen's right edge, and
    // clamped vertically against that screen's bottom edge the same way.
    void showPreview(const QPixmap& pixmap, const QPoint& globalAnchorPos);

    // Always-animated GIF hover (Architecture.md §14.79): plays `path` directly via its own
    // QMovie, bypassing the async decode-then-showPreview() pipeline entirely so a hovered GIF
    // always animates rather than showing a single static frame.
    void showAnimatedGif(const std::filesystem::path& path, const QPoint& globalAnchorPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void positionAndShow(const QSize& contentSize, const QPoint& globalAnchorPos);

    QPixmap m_pixmap; // pre-scaled to fit the max display size, aspect preserved
    QMovie* m_movie = nullptr;
    QPoint m_movieAnchorPos;
};
