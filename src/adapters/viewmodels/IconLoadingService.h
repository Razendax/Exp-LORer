#pragma once

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

class IconLoadingWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as GitStatusService/DiskUsageService/FragmentationService). Resolves
// QFileIconProvider icons for a directory listing's rows off the Qt UI thread, in small chunks, so
// opening an executable-heavy folder (each .exe/.dll/.lnk needing its own unique PE-resource icon
// extraction, not a cheap shared type icon) no longer blocks the UI thread on first paint
// ("Fix slow folder-open for executable-heavy directories").
class IconLoadingService : public QObject
{
    Q_OBJECT

public:
    explicit IconLoadingService(QObject* parent = nullptr);
    ~IconLoadingService() override;

    // Dispatches chunked (~20 entries each) icon-resolution requests for `entries` and delivers
    // each chunk's results back to `context`'s thread via `onBatch` as they complete, so rows
    // light up progressively instead of the view waiting for the whole directory. `context` is
    // watched via QPointer for destruction: if it has been destroyed by the time a chunk's reply
    // comes back, that reply is silently skipped -- same guard idiom as
    // GitStatusService::requestStatus. Callers pass a distinct, stable `context` per logical
    // result stream they maintain (e.g. one of TabViewModel's own FileListModel instances) so
    // concurrent streams from the same tab (normal browsing vs. quick search vs. advanced search)
    // can't invalidate each other's chunks below.
    //
    // `directory` tags the request for a lightweight staleness check, scoped per `context`: before
    // processing a chunk, the worker checks whether `directory` is still the most recently
    // requested one *for that context* and silently drops the chunk if a newer request (for a
    // different directory, from the same context) has since superseded it. There is no per-request
    // cancel flag (unlike DiskUsageService's scan cancellation) -- deliberately simpler, since a
    // chunk already in flight just finishes (≤20 icons) and its reply is also discarded by the
    // stale-path/stale-generation check every caller already applies on its own side.
    void requestIcons(QObject* context, const std::filesystem::path& directory, std::vector<FileNode> entries,
                       std::function<void(QHash<QString, QIcon> batch)> onBatch);

private:
    QThread m_workerThread;
    IconLoadingWorker* m_worker = nullptr;
    QMutex m_activeDirectoryMutex;
    // Latest requested directory per context, compared by pointer value only (never dereferenced) --
    // safe to keep an entry around after its context is destroyed, since a stale entry is simply
    // overwritten the next time that same context (or, vanishingly unlikely, a reused address)
    // issues a new request before any of its chunks are checked.
    QHash<QObject*, std::filesystem::path> m_activeDirectoryByContext;
};
