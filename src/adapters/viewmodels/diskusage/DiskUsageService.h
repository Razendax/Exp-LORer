#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>

#include "CancellableScanService.h"
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
//
// Thread/cancel-flag/callback-bouncing plumbing lives in CancellableScanService, shared with
// DuplicateFinderService/FragmentationService (Architecture.md §14.70).
class DiskUsageService : public CancellableScanService
{
    Q_OBJECT

public:
    DiskUsageService(IDiskUsageScanner& scanner, const std::filesystem::path& databasePath, QObject* parent = nullptr);

    // requestId is caller-supplied (each per-tab FolderSizeViewModel passes the NotificationCenter
    // ProgressId it creates for its own scan, same "ids line up 1:1" posture as
    // FragmentationService) so cancel() needs no second map on the caller's side. onProgress/
    // onFinished are bounced back to `context`'s thread, guarded so neither fires after `context`
    // is destroyed.
    void requestScan(QObject* context, int requestId, const std::filesystem::path& root,
                      std::function<void(std::size_t scanned, const std::filesystem::path& currentPath)> onProgress,
                      std::function<void(Result<void>)> onFinished);

    // Cached immediate children of `folder` from the most recent scan that covered it.
    void requestChildren(QObject* context, const std::filesystem::path& folder,
                          std::function<void(Result<std::vector<DiskUsageNode>>)> callback);

private:
    DiskUsageWorker* m_worker = nullptr;
};
