#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>

#include "CancellableScanService.h"
#include "DuplicateFinderWorker.h"
#include "Result.h"

class DuplicateFinderUseCase;
class IFileSystemRepository;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown -- same
// lifetime posture as FileTransferService). Runs Duplicate Finder scans (Architecture.md §14.51) on
// one dedicated background QThread, off the Qt UI thread. One thread means a second scan request
// while one is in flight queues behind it -- in practice never reached, since DuplicateFinderDialog
// disables its Scan buttons for the duration of a scan, same posture as FolderSizeWidget.
//
// Thread/cancel-flag/callback-bouncing plumbing lives in CancellableScanService, shared with
// DiskUsageService/FragmentationService (Architecture.md §14.70).
class DuplicateFinderService : public CancellableScanService
{
    Q_OBJECT

public:
    DuplicateFinderService(DuplicateFinderUseCase& useCase, IFileSystemRepository& fileSystemRepository,
                            QObject* parent = nullptr);

    struct Callbacks
    {
        std::function<void(std::size_t hashed, std::size_t totalCandidates)> onProgress;
        std::function<void(Result<std::vector<DuplicateGroupInfo>> result)> onFinished;
    };

    // requestId is caller-supplied (DuplicateFinderDialog passes the NotificationCenter ProgressId
    // it already created, same "ids line up 1:1" posture as FileTransferService::requestTransfer) so
    // cancel() needs no second map on the caller's side. Every Callbacks member is bounced back to
    // `context`'s thread, guarded so none fire after `context` is destroyed.
    void requestScan(QObject* context, int requestId, const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
                      Callbacks callbacks);

private:
    DuplicateFinderWorker* m_worker = nullptr;
};
