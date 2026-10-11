#pragma once

#include <QPixmap>
#include <QWidget>

// Frameless, non-interactive popup showing an image/video file's preview beside the mouse cursor
// while hovering its row in the normal file browser (Architecture.md §14.76). Modeled on the
// frameless-popup precedent in AddressBarWidget/TagSearchEdit, but a static display rather than an
// interactive list: WA_TransparentForMouseEvents means it never steals hover/click from the view
// underneath it.
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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap m_pixmap; // pre-scaled to fit the max display size, aspect preserved
};
