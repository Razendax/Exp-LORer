#include "FragmentationService.h"

#include <utility>

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>

#include "FragmentationWorker.h"
#include "Logging.h"

FragmentationService::FragmentationService(IFragmentationScanner& scanner, const std::filesystem::path& databasePath,
                                             QObject* parent)
    : QObject(parent)
{
    m_worker = new FragmentationWorker(scanner, databasePath);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

FragmentationService::~FragmentationService()
{
    // m_worker's lazily-opened SQLiteFragmentationRepository connection is thread-affine to the
    // worker thread, so it must be torn down there too -- deleteLater() (thread-safe to call from
    // here) queues the deletion on the worker thread's event loop, ahead of the quit() event below.
    m_worker->deleteLater();
    m_workerThread.quit();
    m_workerThread.wait();
}

void FragmentationService::requestScan(
    QObject* context, int requestId, const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    std::function<void(std::size_t, std::size_t, const std::filesystem::path&)> onProgress,
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
        [this, guard, requestId, root, minFileSizeBytes, cancelFlag, onProgress = std::move(onProgress),
         onFinished = std::move(onFinished)]() {
            // Called synchronously on this worker thread from inside scanAndPersist below -- bounce
            // the progress callback back to the requesting side the same way the final result is,
            // so it never runs on the wrong thread and never fires after `context` is gone.
            auto boundProgress = [this, guard, onProgress](std::size_t scanned, std::size_t totalCandidates,
                                                             const std::filesystem::path& currentPath) {
                if (!onProgress)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [guard, onProgress, scanned, totalCandidates, currentPath]() {
                        if (!guard)
                        {
                            return;
                        }
                        onProgress(scanned, totalCandidates, currentPath);
                    },
                    Qt::QueuedConnection);
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

void FragmentationService::cancel(int requestId)
{
    QMutexLocker locker(&m_cancelFlagsMutex);
    if (auto it = m_cancelFlags.find(requestId); it != m_cancelFlags.end())
    {
        it.value()->store(true, std::memory_order_relaxed);
    }
}

void FragmentationService::requestFilesUnder(QObject* context, const std::filesystem::path& folder,
                                              std::function<void(Result<std::vector<FileFragmentationInfo>>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, folder, callback = std::move(callback)]() {
            Result<std::vector<FileFragmentationInfo>> result = m_worker->filesUnder(folder);

            if (result.hasError())
            {
                Logging::warn("FragmentationService: filesUnder failed: " + std::string(toLogString(result.error().code)));
            }
            else
            {
                Logging::debug("FragmentationService: filesUnder returned " + std::to_string(result.value().size()) + " file(s)");
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
