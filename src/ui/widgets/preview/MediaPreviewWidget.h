#pragma once

#include <filesystem>

#include <QPixmap>
#include <QWidget>

class FilePreview;
class GifPlayerWidget;
class QGroupBox;
class QLabel;
class QResizeEvent;
class QStackedWidget;
class VideoPlayerWidget;

// The image/video page of the Preview panel's stack (Architecture.md §14.26/§14.58/§14.85,
// Specification.md "Preview Panel"): the full filename, a content stack switching between the
// static poster frame (image or video, identical treatment), the inline VideoPlayerWidget, and
// the inline GifPlayerWidget, a collapsible properties block (resolution/bitrate/size), and a
// live zoom-percentage label, top to bottom. The zoom label is computed from the content stack's
// own size against the actual resolution (not just the static image's rendered pixmap), so it
// keeps showing a value across all three content modes instead of disappearing when the user
// flips to Video player/Animation; it's hidden only when there is no resolution metadata at all.
class MediaPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MediaPreviewWidget(QWidget* parent = nullptr);

    // Updates the name label + Properties box text from `preview`'s metadata. Independent of
    // which content sub-page is showing, so it must be called once per preview regardless of
    // which mode (static/video/gif) the caller is about to show.
    void setMetadata(const FilePreview& preview);

    // Decodes preview.imageBytes() and switches the content stack to the static image page.
    // Returns false (content stack left untouched) if decoding fails, so the caller can fall back
    // to its own shared unsupported label.
    bool showStaticImage(const FilePreview& preview);

    // Loads and switches the content stack to the inline video player.
    void showVideo(const std::filesystem::path& path);

    // Loads and switches the content stack to the inline GIF player.
    void showGifAnimation(const std::filesystem::path& path);

    // Forwarding methods preserving PreviewPanelWidget's existing release call sites now that the
    // players are owned here.
    void releaseVideoPlayer();
    void releaseGifPlayer();
    void releaseVideoPlayerIfActive();
    void releaseGifPlayerIfActive();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void rescaleImage();
    void updateZoomLabel();

    QLabel* m_nameLabel = nullptr;            // objectName "previewImageNameLabel"
    QStackedWidget* m_contentStack = nullptr; // objectName "previewContentStack"
    QLabel* m_imageLabel = nullptr;           // objectName "previewImageLabel"
    VideoPlayerWidget* m_videoPlayerWidget = nullptr;
    GifPlayerWidget* m_gifPlayerWidget = nullptr;
    QGroupBox* m_propertiesGroupBox = nullptr; // objectName "previewImagePropertiesGroupBox"
    QLabel* m_propertiesLabel = nullptr;      // objectName "previewImagePropertiesLabel"
    QLabel* m_zoomLabel = nullptr;            // objectName "previewZoomLabel"

    QPixmap m_currentImage;
    // The previewed file's actual resolution (0 when metadata extraction failed) -- used by
    // rescaleImage() to recompute the zoom ratio on every resize.
    int m_currentActualWidth = 0;
    int m_currentActualHeight = 0;
};
