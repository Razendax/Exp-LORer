#pragma once

#include <chrono>
#include <vector>

#include <QImage>
#include <QMetaType>

// Per-row Media Viewer grid thumbnail payload (Architecture.md §14.62): a decoded poster-frame/
// image preview plus the metadata MediaGridTileDelegate overlays as corner labels (resolution for
// images, duration for videos). image is a plain QImage (no GUI-thread affinity) rather than a
// QPixmap/QIcon, so it can travel from MediaThumbnailLoadingService's worker thread the same way
// IconLoadingWorker's IconImages does; width/height/duration come from the MediaThumbnail::
// metadata bundled with MediaProcessingUseCase::generateThumbnail()'s result, not from
// image.size() (the decoded thumbnail is scaled down, the real dimensions aren't).
//
// previewFrames (Architecture.md §14.64) is empty for an image row -- image stays the only frame,
// unchanged behavior -- and holds MediaPreviewFrames::kCount entries in time order for a video row
// (previewFrames[0] == image, decoded once and reused rather than duplicated), letting
// MediaGridTileDelegate paint whichever frame the cursor is currently hovering over.
struct MediaThumbnailData
{
    QImage image;
    int width = 0;
    int height = 0;
    std::chrono::milliseconds duration{};
    std::vector<QImage> previewFrames;
};

Q_DECLARE_METATYPE(MediaThumbnailData)
