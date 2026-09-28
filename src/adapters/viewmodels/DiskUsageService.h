#pragma once

#include <filesystem>
#include <functional>
#include <vector>

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

    // Scans `root` and persists the result. Discarded (callback never invoked) if `context` is
    // destroyed before the scan finishes.
    void requestScan(QObject* context, const std::filesystem::path& root, std::function<void(Result<void>)> callback);

    // Cached immediate children of `folder` from the most recent scan that covered it.
    void requestChildren(QObject* context, const std::filesystem::path& folder,
                          std::function<void(Result<std::vector<DiskUsageNode>>)> callback);

private:
    QThread m_workerThread;
    DiskUsageWorker* m_worker = nullptr;
};
