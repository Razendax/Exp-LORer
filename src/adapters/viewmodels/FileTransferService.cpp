#include "FileTransferService.h"

#include <utility>

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>

#include "FileTransferWorker.h"

FileTransferService::FileTransferService(IFileSystemRepository& fileSystemRepository, QObject* parent)
    : QObject(parent)
{
    m_worker = new FileTransferWorker(fileSystemRepository);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

FileTransferService::~FileTransferService()
{
    m_worker->deleteLater();
    m_workerThread.quit();
    m_workerThread.wait();
}

void FileTransferService::requestTransfer(QObject* context, int requestId,
                                           std::vector<std::pair<std::filesystem::path, std::filesystem::path>> topLevelRoots,
                                           bool isMove, Callbacks callbacks)
{
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    {
        QMutexLocker locker(&m_cancelFlagsMutex);
        m_cancelFlags.insert(requestId, cancelFlag);
    }

    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, requestId, topLevelRoots = std::move(topLevelRoots), isMove, cancelFlag,
         callbacks = std::move(callbacks)]() {
            // Called synchronously on this worker thread from inside runTransfer below -- bounce
            // each callback back to the requesting side the same way the final result is, so none
            // of them ever run on the wrong thread and none fire after `context` is gone.
            auto onEstimateProgress = [this, guard, callback = callbacks.onEstimateProgress](std::size_t filesSoFar,
                                                                                                std::uintmax_t bytesSoFar) {
                if (!callback)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [guard, callback, filesSoFar, bytesSoFar]() {
                        if (!guard)
                        {
                            return;
                        }
                        callback(filesSoFar, bytesSoFar);
                    },
                    Qt::QueuedConnection);
            };

            auto onInsufficientSpace = [this, guard, callback = callbacks.onInsufficientSpace](std::uintmax_t requiredBytes,
                                                                                                  std::uintmax_t availableBytes) {
                if (!callback)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [guard, callback, requiredBytes, availableBytes]() {
                        if (!guard)
                        {
                            return;
                        }
                        callback(requiredBytes, availableBytes);
                    },
                    Qt::QueuedConnection);
            };

            auto onFileStarted = [this, guard, callback = callbacks.onFileStarted](const FileTransferItem& item) {
                if (!callback)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [guard, callback, item]() {
                        if (!guard)
                        {
                            return;
                        }
                        callback(item);
                    },
                    Qt::QueuedConnection);
            };

            auto onBytesProgress = [this, guard, callback = callbacks.onBytesProgress](std::uintmax_t bytesCopiedSoFar,
                                                                                         std::uintmax_t totalBytes) {
                if (!callback)
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [guard, callback, bytesCopiedSoFar, totalBytes]() {
                        if (!guard)
                        {
                            return;
                        }
                        callback(bytesCopiedSoFar, totalBytes);
                    },
                    Qt::QueuedConnection);
            };

            Result<void> result = m_worker->runTransfer(topLevelRoots, isMove, cancelFlag, onEstimateProgress,
                                                          onInsufficientSpace, onFileStarted, onBytesProgress);

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

void FileTransferService::cancel(int requestId)
{
    QMutexLocker locker(&m_cancelFlagsMutex);
    if (auto it = m_cancelFlags.find(requestId); it != m_cancelFlags.end())
    {
        it.value()->store(true, std::memory_order_relaxed);
    }
}
