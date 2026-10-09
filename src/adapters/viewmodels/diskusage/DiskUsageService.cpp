#include "DiskUsageService.h"

#include <utility>

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>

#include "DiskUsageWorker.h"
#include "Logging.h"

DiskUsageService::DiskUsageService(IDiskUsageScanner& scanner, const std::filesystem::path& databasePath, QObject* parent)
    : QObject(parent)
{
    m_worker = new DiskUsageWorker(scanner, databasePath);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

DiskUsageService::~DiskUsageService()
{
    // m_worker's lazily-opened SQLiteDiskUsageRepository connection is thread-affine to the worker
    // thread, so it must be torn down there too -- deleteLater() (thread-safe to call from here)
    // queues the deletion on the worker thread's event loop, ahead of the quit() event below.
    m_worker->deleteLater();
    m_workerThread.quit();
    m_workerThread.wait();
}

void DiskUsageService::requestScan(QObject* context, int requestId, const std::filesystem::path& root,
                                    std::function<void(std::size_t, const std::filesystem::path&)> onProgress,
                                    std::function<void(Result<void>)> onFinished)
{
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    {
        QMutexLocker locker(&m_cancelFlagsMutex);
        m_cancelFlags.insert(requestId, cancelFlag);
    }

    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, requestId, root, cancelFlag, onProgress = std::move(onProgress), onFinished = std::move(onFinished)]() {
            // Called synchronously on this worker thread from inside scanAndPersist below -- bounce
            // the progress callback back to the requesting side the same way the final result is,
            // so it never runs on the wrong thread and never fires after `context` is gone.
            auto boundProgress = [this, guard, onProgress](std::size_t scanned, const std::filesystem::path& currentPath) {
                if (!onProgress)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [guard, onProgress, scanned, currentPath]() {
                        if (!guard)
                        {
                            return;
                        }
                        onProgress(scanned, currentPath);
                    },
                    Qt::QueuedConnection);
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

            {
                QMutexLocker locker(&m_cancelFlagsMutex);
                m_cancelFlags.remove(requestId);
            }

            QMetaObject::invokeMethod(
                this,
                [guard, onFinished, result = std::move(result)]() mutable {
                    if (!guard || !onFinished)
                    {
                        return;
                    }
                    onFinished(std::move(result));
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void DiskUsageService::cancel(int requestId)
{
    QMutexLocker locker(&m_cancelFlagsMutex);
    if (auto it = m_cancelFlags.find(requestId); it != m_cancelFlags.end())
    {
        it.value()->store(true, std::memory_order_relaxed);
    }
}

void DiskUsageService::requestChildren(QObject* context, const std::filesystem::path& folder,
                                        std::function<void(Result<std::vector<DiskUsageNode>>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, folder, callback = std::move(callback)]() {
            Result<std::vector<DiskUsageNode>> result = m_worker->childrenFor(folder);

            if (result.hasError())
            {
                Logging::warn("DiskUsageService: childrenFor failed: " + std::string(toLogString(result.error().code)));
            }
            else
            {
                Logging::debug("DiskUsageService: childrenFor returned " + std::to_string(result.value().size()) + " node(s)");
            }

            QMetaObject::invokeMethod(
                this,
                [guard, callback, result = std::move(result)]() mutable {
                    if (!guard)
                    {
                        return;
                    }
                    callback(std::move(result));
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
