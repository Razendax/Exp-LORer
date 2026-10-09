#include "DuplicateFinderService.h"

#include <utility>

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>

#include "Logging.h"

DuplicateFinderService::DuplicateFinderService(DuplicateFinderUseCase& useCase, IFileSystemRepository& fileSystemRepository,
                                                QObject* parent)
    : QObject(parent)
{
    m_worker = new DuplicateFinderWorker(useCase, fileSystemRepository);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

DuplicateFinderService::~DuplicateFinderService()
{
    m_worker->deleteLater();
    m_workerThread.quit();
    m_workerThread.wait();
}

void DuplicateFinderService::requestScan(QObject* context, int requestId, const std::filesystem::path& root,
                                          std::uintmax_t minFileSizeBytes, Callbacks callbacks)
{
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    {
        QMutexLocker locker(&m_cancelFlagsMutex);
        m_cancelFlags.insert(requestId, cancelFlag);
    }

    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, requestId, root, minFileSizeBytes, cancelFlag, callbacks = std::move(callbacks)]() {
            // Called synchronously on this worker thread from inside runScan below -- bounce the
            // progress callback back to the requesting side the same way the final result is, so it
            // never runs on the wrong thread and never fires after `context` is gone.
            auto onProgress = [this, guard, callback = callbacks.onProgress](std::size_t hashed, std::size_t totalCandidates) {
                if (!callback)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [guard, callback, hashed, totalCandidates]() {
                        if (!guard)
                        {
                            return;
                        }
                        callback(hashed, totalCandidates);
                    },
                    Qt::QueuedConnection);
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

            {
                QMutexLocker locker(&m_cancelFlagsMutex);
                m_cancelFlags.remove(requestId);
            }

            QMetaObject::invokeMethod(
                this,
                [guard, onFinished = callbacks.onFinished, result = std::move(result)]() mutable {
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

void DuplicateFinderService::cancel(int requestId)
{
    QMutexLocker locker(&m_cancelFlagsMutex);
    if (auto it = m_cancelFlags.find(requestId); it != m_cancelFlags.end())
    {
        it.value()->store(true, std::memory_order_relaxed);
    }
}
