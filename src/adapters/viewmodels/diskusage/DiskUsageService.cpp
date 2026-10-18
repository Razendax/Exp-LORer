#include "DiskUsageService.h"

#include <utility>

#include "DiskUsageWorker.h"
#include "Logging.h"

DiskUsageService::DiskUsageService(IDiskUsageScanner& scanner, const std::filesystem::path& databasePath, QObject* parent)
    : CancellableScanService(parent)
{
    m_worker = new DiskUsageWorker(scanner, databasePath);
    startWorker(m_worker);
}

void DiskUsageService::requestScan(QObject* context, int requestId, const std::filesystem::path& root,
                                    std::function<void(std::size_t, const std::filesystem::path&)> onProgress,
                                    std::function<void(Result<void>)> onFinished)
{
    auto cancelFlag = registerCancelFlag(requestId);
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, requestId, root, cancelFlag, onProgress = std::move(onProgress), onFinished = std::move(onFinished)]() {
        // Called synchronously on this worker thread from inside scanAndPersist below -- bounce
        // the progress callback back to the requesting side the same way the final result is, so
        // it never runs on the wrong thread and never fires after `context` is gone.
        auto boundProgress = [this, guard, onProgress](std::size_t scanned, const std::filesystem::path& currentPath) {
            invokeGuarded(guard, onProgress, scanned, currentPath);
        };

        Result<void> result = m_worker->scanAndPersist(root, cancelFlag, boundProgress);

        if (result.hasError())
        {
            Logging::warn("DiskUsageService: scan failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("DiskUsageService: scan finished");
        }

        unregisterCancelFlag(requestId);

        invokeGuarded(guard, onFinished, std::move(result));
    });
}

void DiskUsageService::requestChildren(QObject* context, const std::filesystem::path& folder,
                                        std::function<void(Result<std::vector<DiskUsageNode>>)> callback)
{
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, folder, callback = std::move(callback)]() {
        Result<std::vector<DiskUsageNode>> result = m_worker->childrenFor(folder);

        if (result.hasError())
        {
            Logging::warn("DiskUsageService: childrenFor failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("DiskUsageService: childrenFor returned " + std::to_string(result.value().size()) + " node(s)");
        }

        invokeGuarded(guard, callback, std::move(result));
    });
}
