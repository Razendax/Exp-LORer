#include "FragmentationService.h"

#include <utility>

#include "FragmentationWorker.h"
#include "Logging.h"

FragmentationService::FragmentationService(IFragmentationScanner& scanner, const std::filesystem::path& databasePath,
                                             QObject* parent)
    : CancellableScanService(parent)
{
    m_worker = new FragmentationWorker(scanner, databasePath);
    startWorker(m_worker);
}

void FragmentationService::requestScan(
    QObject* context, int requestId, const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    std::function<void(std::size_t, std::size_t, const std::filesystem::path&)> onProgress,
    std::function<void(Result<void>)> onFinished)
{
    auto cancelFlag = registerCancelFlag(requestId);
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, requestId, root, minFileSizeBytes, cancelFlag, onProgress = std::move(onProgress),
                 onFinished = std::move(onFinished)]() {
        // Called synchronously on this worker thread from inside scanAndPersist below -- bounce
        // the progress callback back to the requesting side the same way the final result is, so
        // it never runs on the wrong thread and never fires after `context` is gone.
        auto boundProgress = [this, guard, onProgress](std::size_t scanned, std::size_t totalCandidates,
                                                         const std::filesystem::path& currentPath) {
            invokeGuarded(guard, onProgress, scanned, totalCandidates, currentPath);
        };

        Result<void> result = m_worker->scanAndPersist(root, minFileSizeBytes, cancelFlag, boundProgress);

        if (result.hasError())
        {
            Logging::warn("FragmentationService: scan failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("FragmentationService: scan finished");
        }

        unregisterCancelFlag(requestId);

        invokeGuarded(guard, onFinished, std::move(result));
    });
}

void FragmentationService::requestFilesUnder(QObject* context, const std::filesystem::path& folder,
                                              std::function<void(Result<std::vector<FileFragmentationInfo>>)> callback)
{
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, folder, callback = std::move(callback)]() {
        Result<std::vector<FileFragmentationInfo>> result = m_worker->filesUnder(folder);

        if (result.hasError())
        {
            Logging::warn("FragmentationService: filesUnder failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("FragmentationService: filesUnder returned " + std::to_string(result.value().size()) + " file(s)");
        }

        invokeGuarded(guard, callback, std::move(result));
    });
}
