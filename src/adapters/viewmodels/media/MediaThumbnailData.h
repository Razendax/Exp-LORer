#pragma once

#include <chrono>

#include <QImage>
#include <QMetaType>

// Per-row Media Viewer grid thumbnail payload (Architecture.md §14.62): a decoded poster-frame/
// image preview plus the metadata MediaGridTileDelegate overlays as corner labels (resolution for
// images, duration for videos). image is a plain QImage (no GUI-thread affinity) rather than a
// QPixmap/QIcon, so it can travel from MediaThumbnailLoadingService's worker thread the same way
// IconLoadingWorker's IconImages does; width/height/duration come from MediaProcessingUseCase::
// extractMetadata, not from image.size() (the decoded thumbnail is scaled down, the real dimensions
// aren't).
struct MediaThumbnailData
{
    QImage image;
    int width = 0;
    int height = 0;
    std::chrono::milliseconds duration{};
};

Q_DECLARE_METATYPE(MediaThumbnailData)
