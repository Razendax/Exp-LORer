#include "DuplicateFinderService.h"

#include <utility>

#include "Logging.h"

DuplicateFinderService::DuplicateFinderService(DuplicateFinderUseCase& useCase, IFileSystemRepository& fileSystemRepository,
                                                QObject* parent)
    : CancellableScanService(parent)
{
    m_worker = new DuplicateFinderWorker(useCase, fileSystemRepository);
    startWorker(m_worker);
}

void DuplicateFinderService::requestScan(QObject* context, int requestId, const std::filesystem::path& root,
                                          std::uintmax_t minFileSizeBytes, Callbacks callbacks)
{
    auto cancelFlag = registerCancelFlag(requestId);
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, requestId, root, minFileSizeBytes, cancelFlag, callbacks = std::move(callbacks)]() {
        // Called synchronously on this worker thread from inside runScan below -- bounce the
        // progress callback back to the requesting side the same way the final result is, so it
        // never runs on the wrong thread and never fires after `context` is gone.
        auto onProgress = [this, guard, callback = callbacks.onProgress](std::size_t hashed, std::size_t totalCandidates) {
            invokeGuarded(guard, callback, hashed, totalCandidates);
        };

        Result<std::vector<DuplicateGroupInfo>> result = m_worker->runScan(root, minFileSizeBytes, cancelFlag, onProgress);

        if (result.hasError())
        {
            Logging::warn("DuplicateFinderService: scan failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            std::size_t totalFiles = 0;
            for (const DuplicateGroupInfo& groupInfo : result.value())
            {
                totalFiles += groupInfo.files.size();
            }
            Logging::info("DuplicateFinderService: scan finished (" + std::to_string(result.value().size()) +
                           " group(s), " + std::to_string(totalFiles) + " file(s))");
        }

        unregisterCancelFlag(requestId);

        invokeGuarded(guard, callbacks.onFinished, std::move(result));
    });
}
