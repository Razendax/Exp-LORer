#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThread>

#include "FileTransferTypes.h"
#include "Result.h"

class FileTransferWorker;
class IFileSystemRepository;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as AdvancedSearchService). Runs the custom copy/move pipeline
// (Architecture.md §14.47) on one dedicated background QThread, off the Qt UI thread. One thread
// means two concurrent pastes queue serially behind each other -- an accepted limitation, not a
// thread pool, consistent with AdvancedSearchService/FileTagsService.
class FileTransferService : public QObject
{
    Q_OBJECT

public:
    explicit FileTransferService(IFileSystemRepository& fileSystemRepository, QObject* parent = nullptr);
    ~FileTransferService() override;

    struct Callbacks
    {
        std::function<void(std::size_t filesSoFar, std::uintmax_t bytesSoFar)> onEstimateProgress;
        std::function<void(std::uintmax_t requiredBytes, std::uintmax_t availableBytes)> onInsufficientSpace;
        std::function<void(const FileTransferItem& currentFile)> onFileStarted;
        std::function<void(std::uintmax_t bytesCopiedSoFar, std::uintmax_t totalBytes)> onBytesProgress;
        std::function<void(Result<void> finalResult)> onFinished; // Cancelled is a normal finalResult, not a popup error.
    };

    // requestId is caller-supplied (FileOperationsController passes the NotificationCenter
    // ProgressId it already created, so the two ids line up 1:1) rather than generated here --
    // simplifies cancel() lookups and keeps the two systems correlated without a second map on the
    // caller's side. Every Callbacks member is bounced back to `context`'s thread via
    // QMetaObject::invokeMethod, guarded by a QPointer so none fire after `context` is destroyed --
    // same pattern as AdvancedSearchService::requestRecursiveScan.
    void requestTransfer(QObject* context, int requestId,
                          std::vector<std::pair<std::filesystem::path, std::filesystem::path>> topLevelRoots, bool isMove,
                          Callbacks callbacks);

    // Flips the matching cancel flag if `requestId` is still in flight; a no-op otherwise (already
    // finished, or unknown id) -- safe to call blindly. std::atomic<bool> is safe to flip directly
    // from the UI thread; the worker thread's loop observes it on its own next check, no thread hop
    // needed here.
    void cancel(int requestId);

private:
    QThread m_workerThread;
    FileTransferWorker* m_worker = nullptr;
    QMutex m_cancelFlagsMutex;
    QHash<int, std::shared_ptr<std::atomic<bool>>> m_cancelFlags; // guarded by m_cancelFlagsMutex
};
