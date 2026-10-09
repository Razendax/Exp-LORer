#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>

#include "FileNode.h"
#include "MediaThumbnailData.h"

class MediaProcessingUseCase;
class MediaThumbnailLoadingWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as GitStatusService/DiskUsageService/FragmentationService). Decodes Media
// Viewer grid thumbnails (Architecture.md §14.62) off the Qt UI thread, in small chunks, over the
// already-shared MediaProcessingUseCase/CachingMediaDecoder instance -- same decode+cache path the
// Preview panel already uses.
//
// One dedicated worker QThread, not a pool (unlike IconLoadingService): every chunk goes through
// the same shared MediaProcessingUseCase/CachingMediaDecoder instance, so running chunks
// concurrently wouldn't parallelize actual decode work the way IconLoadingService's pool
// parallelizes independent synchronous shell/GDI calls -- it would just contend for the same
// decoder/cache. A single serialized worker is the safer, more consistent default, matching every
// other background *Service in this codebase (GitStatusService/DiskUsageService/
// FragmentationService/FilePreviewWorker).
class MediaThumbnailLoadingService : public QObject
{
    Q_OBJECT

public:
    explicit MediaThumbnailLoadingService(MediaProcessingUseCase& mediaProcessingUseCase, QObject* parent = nullptr);
    ~MediaThumbnailLoadingService() override;

    // Dispatches chunked (~10 entries each -- smaller than IconLoadingService's 20, since decode is
    // heavier per item) thumbnail-decode requests for `entries`, delivering each chunk's results
    // back to `context`'s thread via `onBatch` as they complete, so grid tiles light up
    // progressively instead of the view waiting for the whole result set. `context` is watched via
    // QPointer for destruction (a reply for a destroyed context is silently skipped), and `root`
    // tags the request for a lightweight per-context staleness check -- a chunk still in flight is
    // dropped if a newer request for a different root, from the same context, has since superseded
    // it. Same guard idioms as IconLoadingService::requestIcons.
    void requestThumbnails(QObject* context, const std::filesystem::path& root, std::vector<FileNode> entries, int maxWidth,
                            int maxHeight, std::function<void(QHash<QString, MediaThumbnailData> batch)> onBatch);

private:
    QThread m_workerThread;
    MediaThumbnailLoadingWorker* m_worker = nullptr;

    QMutex m_activeRootMutex;
    // Latest requested root per context, compared by pointer value only (never dereferenced) --
    // safe to keep an entry around after its context is destroyed, same posture as
    // IconLoadingService::m_activeDirectoryByContext.
    QHash<QObject*, std::filesystem::path> m_activeRootByContext;
};
