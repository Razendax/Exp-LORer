#pragma once

#include <vector>

#include <QHash>
#include <QObject>
#include <QString>

#include "FileNode.h"
#include "MediaThumbnailData.h"

class MediaProcessingUseCase;

// Decodes Media Viewer grid thumbnails off the Qt UI thread via the already-shared
// MediaProcessingUseCase/CachingMediaDecoder instance (Architecture.md §14.62) -- same decode+cache
// path FilePreviewWorker already uses for the Preview panel. Owned and driven by
// MediaThumbnailLoadingService, same shape as IconLoadingWorker/GitStatusWorker.
class MediaThumbnailLoadingWorker : public QObject
{
    Q_OBJECT

public:
    explicit MediaThumbnailLoadingWorker(MediaProcessingUseCase& mediaProcessingUseCase, QObject* parent = nullptr);

    // Resolves a thumbnail + basic metadata for every entry in `entries`, keyed by each entry's
    // absolute path. A decode/metadata failure for one entry simply omits it from the result (same
    // "no row update" posture as a cache-miss row in FileListModel::resolveIcon) -- its placeholder
    // icon just keeps showing.
    QHash<QString, MediaThumbnailData> thumbnailsFor(const std::vector<FileNode>& entries, int maxWidth, int maxHeight) const;

private:
    MediaProcessingUseCase& m_mediaProcessingUseCase;
};
