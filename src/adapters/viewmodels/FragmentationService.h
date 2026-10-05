#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThread>

#include "FileFragmentationInfo.h"
#include "Result.h"

class IFragmentationScanner;
class FragmentationWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime as DiskUsageService). Runs the Fragmentation bottom-panel tab's scan/lookup
// requests (Architecture.md §14.52) on one dedicated background QThread, off the Qt UI thread.
//
// A synthesis of two existing patterns: DiskUsageWorker/DiskUsageService's lazy own-thread SQLite
// connection, and DuplicateFinderService/DuplicateFinderWorker's real per-request
// std::atomic<bool> cancel flag + live progress callback (needed because the per-file
// DeviceIoControl cost is real, unlike Disk Usage's plain FindFirstFileW walk, which has no cancel
// in v1). Multiple tabs can each have a scan in flight concurrently -- different tabs' requests
// carry different requestIds and simply queue on this one worker thread, same "single dedicated
// thread serializes concurrent per-tab requests" posture DiskUsageService/AdvancedSearchWorker
// already accept.
class FragmentationService : public QObject
{
    Q_OBJECT

public:
    FragmentationService(IFragmentationScanner& scanner, const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~FragmentationService() override;

    // requestId is caller-supplied (each per-tab FragmentationViewModel passes the
    // NotificationCenter ProgressId it creates for its own scan, same "ids line up 1:1" posture as
    // DuplicateFinderDialog) so cancel() needs no second map on the caller's side. onProgress/
    // onFinished are bounced back to `context`'s thread via QMetaObject::invokeMethod, guarded by
    // a QPointer so neither fires after `context` is destroyed. onFinished's own result is
    // success/failure only -- the (potentially huge) scanned file list is never marshaled back
    // over this path; a caller issues a separate requestFilesUnder call afterward to actually
    // populate the UI.
    void requestScan(
        QObject* context, int requestId, const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)> onProgress,
        std::function<void(Result<void>)> onFinished);

    // Flips the matching cancel flag if `requestId` is still in flight; a no-op otherwise -- safe
    // to call blindly, same posture as DuplicateFinderService::cancel.
    void cancel(int requestId);

    // Cached files strictly under `folder` from the most recent scan(s) that covered them. Also
    // the call a successful requestScan's caller issues afterward to actually populate the UI --
    // this is the same lookup a plain navigation-triggered cache redisplay uses, so one code path
    // renders results whether they came from a fresh scan or not.
    void requestFilesUnder(QObject* context, const std::filesystem::path& folder,
                            std::function<void(Result<std::vector<FileFragmentationInfo>>)> callback);

private:
    QThread m_workerThread;
    FragmentationWorker* m_worker = nullptr;
    QMutex m_cancelFlagsMutex;
    QHash<int, std::shared_ptr<std::atomic<bool>>> m_cancelFlags; // guarded by m_cancelFlagsMutex
};
