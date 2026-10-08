#pragma once

#include <QPixmap>
#include <QWidget>

class FilePreview;
class QLabel;
class QResizeEvent;

// The image/video page of the Preview panel's stack (Architecture.md §14.26/§14.58,
// Specification.md "Preview Panel"): a JPEG poster frame (image or video, identical treatment)
// plus an info line and a live zoom-percentage label that both update on resize.
class MediaPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MediaPreviewWidget(QWidget* parent = nullptr);

    // Decodes preview.imageBytes() and updates the info/zoom labels. Returns false (widget left
    // untouched) if decoding fails, so the caller can fall back to its own shared unsupported label.
    bool setPreview(const FilePreview& preview);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void rescaleImage();

    QLabel* m_infoLabel = nullptr;  // objectName "previewImageInfoLabel"
    QLabel* m_imageLabel = nullptr; // objectName "previewImageLabel"
    QLabel* m_zoomLabel = nullptr;  // objectName "previewZoomLabel"

    QPixmap m_currentImage;
    // The previewed file's actual resolution (0 when metadata extraction failed) -- used by
    // rescaleImage() to recompute the zoom ratio on every resize.
    int m_currentActualWidth = 0;
    int m_currentActualHeight = 0;
};
