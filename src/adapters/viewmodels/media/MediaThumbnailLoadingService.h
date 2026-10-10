#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
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
//
// Chunks are not dispatched eagerly -- they sit in an internal priority deque (Architecture.md
// §14.66) and are pumped through the single worker one at a time, so a High-priority request (the
// Media Viewer grid's current kind+name filter match) can jump ahead of a Low-priority request (a
// whole-kind background sweep) already queued, instead of waiting behind its entire backlog.
class MediaThumbnailLoadingService : public QObject
{
    Q_OBJECT

public:
    // High: the current filter match (kind + name query) -- shown on screen right now, so it jumps
    // the queue. Low: a whole-kind background sweep -- only out-prioritized, never discarded.
    enum class Priority
    {
        High,
        Low,
    };

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
    //
    // `priority` places the request's chunks in the internal deque: `High` purges any of this same
    // context's still-queued (not yet started) `High`-priority chunks first, then pushes the new
    // ones to the front -- a newer filter value supersedes the previous one, but never discards a
    // `Low`-priority background sweep's own chunks, which are only ever out-prioritized. `Low`
    // simply appends to the back.
    void requestThumbnails(QObject* context, const std::filesystem::path& root, std::vector<FileNode> entries, int maxWidth,
                            int maxHeight, std::function<void(QHash<QString, MediaThumbnailData> batch)> onBatch,
                            Priority priority);

private:
    struct PendingChunk
    {
        QObject* context = nullptr; // compared by pointer value only, never dereferenced
        QPointer<QObject> guard;
        std::filesystem::path root;
        std::vector<FileNode> chunk;
        int maxWidth = 0;
        int maxHeight = 0;
        std::function<void(QHash<QString, MediaThumbnailData> batch)> onBatch;
        Priority priority = Priority::High;
    };

    // Pops the front of m_pending (if any, and if nothing is already in flight) and posts it to the
    // worker thread; its completion callback flips m_processing back off and calls this again, so
    // exactly one chunk is ever being decoded at a time. Called both when a dispatch arrives (in
    // case the deque was empty/idle) and after each chunk finishes.
    void startNextIfIdle();

    QThread m_workerThread;
    MediaThumbnailLoadingWorker* m_worker = nullptr;

    QMutex m_activeRootMutex;
    // Latest requested root per context, compared by pointer value only (never dereferenced) --
    // safe to keep an entry around after its context is destroyed, same posture as
    // IconLoadingService::m_activeDirectoryByContext.
    QHash<QObject*, std::filesystem::path> m_activeRootByContext;

    QMutex m_pendingMutex;
    std::deque<PendingChunk> m_pending;
    bool m_processing = false;
};
