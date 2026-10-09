#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThread>

#include "DiskUsageNode.h"
#include "Result.h"

class IDiskUsageScanner;
class DiskUsageWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as AdvancedSearchService/GitStatusService). Runs the Folder Size panel's
// scan and cached-children-lookup requests (Architecture.md §14.39) on one dedicated background
// QThread, off the Qt UI thread.
//
// Uses the same context-guarded/callback API as AdvancedSearchService (not FilePreviewViewModel's
// own-signals style) because FolderSizeViewModel instances are destroyed when a tab closes while a
// request could still be in flight -- the QPointer<QObject> guard(context) pattern discards a
// result that arrives after `context` is gone, without needing a manual tabId/generation counter.
class DiskUsageService : public QObject
{
    Q_OBJECT

public:
    DiskUsageService(IDiskUsageScanner& scanner, const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~DiskUsageService() override;

    // requestId is caller-supplied (each per-tab FolderSizeViewModel passes the NotificationCenter
    // ProgressId it creates for its own scan, same "ids line up 1:1" posture as
    // FragmentationService) so cancel() needs no second map on the caller's side. onProgress/
    // onFinished are bounced back to `context`'s thread via QMetaObject::invokeMethod, guarded by a
    // QPointer so neither fires after `context` is destroyed.
    void requestScan(QObject* context, int requestId, const std::filesystem::path& root,
                      std::function<void(std::size_t scanned, const std::filesystem::path& currentPath)> onProgress,
                      std::function<void(Result<void>)> onFinished);

    // Flips the matching cancel flag if `requestId` is still in flight; a no-op otherwise -- safe
    // to call blindly, same posture as FragmentationService::cancel.
    void cancel(int requestId);

    // Cached immediate children of `folder` from the most recent scan that covered it.
    void requestChildren(QObject* context, const std::filesystem::path& folder,
                          std::function<void(Result<std::vector<DiskUsageNode>>)> callback);

private:
    QThread m_workerThread;
    DiskUsageWorker* m_worker = nullptr;
    QMutex m_cancelFlagsMutex;
    QHash<int, std::shared_ptr<std::atomic<bool>>> m_cancelFlags; // guarded by m_cancelFlagsMutex
};
