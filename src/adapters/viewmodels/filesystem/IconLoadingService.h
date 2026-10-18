#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include <QHash>
#include <QIcon>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>

#include "FileNode.h"
#include "IconSizeMask.h"
#include "WorkerThreadPool.h"

class IconLoadingWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as GitStatusService/DiskUsageService/FragmentationService). Resolves
// QFileIconProvider icons for a directory listing's rows off the Qt UI thread, in small chunks, so
// opening an executable-heavy folder (each .exe/.dll/.lnk needing its own unique PE-resource icon
// extraction, not a cheap shared type icon) no longer blocks the UI thread on first paint
// ("Fix slow folder-open for executable-heavy directories").
//
// Unlike every other *Service in this directory (GitStatusService, DiskUsageService,
// FragmentationService, ...), which deliberately use exactly one dedicated worker QThread, this
// one owns a small fixed-size pool: icon extraction is dominated by a synchronous shell/GDI call
// Windows itself doesn't cache, so it benefits linearly from running chunks concurrently in a way
// those other, already-cheap-per-item services don't. Each pool worker keeps its own independent
// IconLoadingWorker cache (no cross-thread sharing/locking) -- the cost is a small, one-time, per-
// worker duplication of a given icon's resolution, not per navigation.
class IconLoadingService : public QObject
{
    Q_OBJECT

public:
    explicit IconLoadingService(QObject* parent = nullptr);
    ~IconLoadingService() override;

    // Dispatches chunked (~20 entries each) icon-resolution requests for `entries`, resolving only
    // the size(s) set in `sizeMask` (see IconSizeMask.h), and delivers each chunk's results back to
    // `context`'s thread via `onBatch` as they complete, so rows light up progressively instead of
    // the view waiting for the whole directory. Chunks are dispatched round-robin across the whole
    // worker pool so a single huge directory's icons actually resolve in parallel, not just icons
    // from separate concurrent requests. `context` is watched via QPointer for destruction: if it
    // has been destroyed by the time a chunk's reply comes back, that reply is silently skipped --
    // same guard idiom as GitStatusService::requestStatus. Callers pass a distinct, stable
    // `context` per logical result stream they maintain (e.g. one of TabViewModel's own
    // FileListModel instances) so concurrent streams from the same tab (normal browsing vs. quick
    // search vs. advanced search) can't invalidate each other's chunks below.
    //
    // `directory` tags the request for a lightweight staleness check, scoped per `context`: before
    // processing a chunk, the worker checks whether `directory` is still the most recently
    // requested one *for that context* and silently drops the chunk if a newer request (for a
    // different directory, from the same context) has since superseded it. There is no per-request
    // cancel flag (unlike DiskUsageService's scan cancellation) -- deliberately simpler, since a
    // chunk already in flight just finishes (≤20 icons) and its reply is also discarded by the
    // stale-path/stale-generation check every caller already applies on its own side.
    void requestIcons(QObject* context, const std::filesystem::path& directory, std::vector<FileNode> entries,
                       IconSizeMask sizeMask, std::function<void(QHash<QString, QIcon> batch)> onBatch);

private:
    // Round-robin cursor across m_pool, incremented per chunk (not per request) so one huge
    // directory's chunks spread across every pool worker. Atomic rather than mutex-guarded since
    // it's on every chunk's dispatch path and needs no synchronization with anything else.
    std::atomic<std::size_t> m_nextWorkerIndex{ 0 };
    QMutex m_activeDirectoryMutex;
    // Latest requested directory per context, compared by pointer value only (never dereferenced) --
    // safe to keep an entry around after its context is destroyed, since a stale entry is simply
    // overwritten the next time that same context (or, vanishingly unlikely, a reused address)
    // issues a new request before any of its chunks are checked.
    QHash<QObject*, std::filesystem::path> m_activeDirectoryByContext;
    // Declared last so it's destroyed first: its dtor (quit()+wait() per worker) must fully stop
    // every pool thread before m_activeDirectoryMutex/m_activeDirectoryByContext above are torn
    // down, since an in-flight chunk on a pool thread still locks/reads them right up to that
    // point. Reversing this order is a real (if narrow) crash: a worker mid-chunk reading a
    // just-destroyed QHash/QMutex when this service itself is destroyed (every run of
    // WorkspaceControllerTest.RenamingAnAncestorFolderMovesTabsBrowsingInsideIt bar app shutdown,
    // where the process exits before teardown even runs).
    WorkerThreadPool<IconLoadingWorker> m_pool;
};
