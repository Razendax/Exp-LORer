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
#include "WorkerThreadPool.h"

class MediaProcessingUseCase;
class MediaThumbnailLoadingWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as GitStatusService/DiskUsageService/FragmentationService). Decodes Media
// Viewer grid thumbnails (Architecture.md §14.62) off the Qt UI thread, in small chunks, over the
// already-shared MediaProcessingUseCase/CachingMediaDecoder instance -- same decode+cache path the
// Preview panel already uses.
//
// A small fixed-size worker pool (Architecture.md §14.78), mirroring IconLoadingService's pool
// pattern -- not the single dedicated QThread this originally shipped with. That original choice
// reasoned a pool would just contend for the same decoder/cache rather than parallelize real work,
// which holds for a cheap-but-many synchronous call but not for FFmpeg video decode: each
// generateThumbnail/generateThumbnails call (§14.77) is CPU-bound and self-contained (its own
// AVFormatContext/decoder), and the only state actually shared across calls, ThumbnailCache, is
// already std::mutex-guarded (§8). Unlike IconLoadingService's pool, every
// MediaThumbnailLoadingWorker instance here is stateless (it only forwards to the shared
// MediaProcessingUseCase&), so there's no per-worker cache to duplicate.
//
// Chunks are not dispatched eagerly -- they sit in an internal priority deque (Architecture.md
// §14.66) and are pumped through whichever pool workers are idle, so a High-priority request (the
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

    // While m_pending and m_idleWorkers are both non-empty, pops one of each and dispatches that
    // chunk to that pool worker; its completion callback returns the worker's index to
    // m_idleWorkers and calls this again, so every idle pool worker stays busy as long as there's
    // a queued chunk (Architecture.md §14.78). Called both when a dispatch arrives (in case a
    // worker was idle) and after each chunk finishes.
    void startNextIfIdle();

    QMutex m_activeRootMutex;
    // Latest requested root per context, compared by pointer value only (never dereferenced) --
    // safe to keep an entry around after its context is destroyed, same posture as
    // IconLoadingService::m_activeDirectoryByContext.
    QHash<QObject*, std::filesystem::path> m_activeRootByContext;

    QMutex m_pendingMutex;
    std::deque<PendingChunk> m_pending;
    // Indices into m_pool that are currently idle, guarded by m_pendingMutex alongside m_pending
    // since both are consulted/mutated together in startNextIfIdle().
    std::deque<std::size_t> m_idleWorkers;

    // Declared last so it's destroyed first: its dtor (quit()+wait() per worker) must fully stop
    // every pool thread before the mutex-guarded members above are torn down, since an in-flight
    // chunk on a pool thread still locks/reads them right up to that point (same ordering bug as
    // IconLoadingService::m_pool -- see its comment).
    WorkerThreadPool<MediaThumbnailLoadingWorker> m_pool;
};
