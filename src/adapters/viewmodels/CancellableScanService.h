#pragma once

#include <atomic>
#include <memory>
#include <tuple>
#include <utility>

#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
#include <QThread>

// Shared thread/cancel-flag/progress-bounce plumbing for the per-feature scan services
// (DiskUsageService, DuplicateFinderService, FragmentationService, Architecture.md §14.70): each
// owns one dedicated background QThread + worker, and issues caller-cancellable scan/lookup
// requests identified by a caller-supplied requestId (the NotificationCenter ProgressId it already
// created), guarding callbacks against a QObject* `context` that may be destroyed before the
// worker replies.
class CancellableScanService : public QObject
{
    Q_OBJECT

public:
    explicit CancellableScanService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~CancellableScanService() override
    {
        // The worker's possibly lazily-opened SQLite connection is thread-affine to the worker
        // thread, so it must be torn down there too -- deleteLater() (thread-safe to call from
        // here) queues the deletion on the worker thread's event loop, ahead of the quit() below.
        if (m_workerObject)
        {
            m_workerObject->deleteLater();
        }
        m_workerThread.quit();
        m_workerThread.wait();
    }

    // Flips the matching cancel flag if `requestId` is still in flight; a no-op otherwise -- safe
    // to call blindly.
    void cancel(int requestId)
    {
        QMutexLocker locker(&m_cancelFlagsMutex);
        if (auto it = m_cancelFlags.find(requestId); it != m_cancelFlags.end())
        {
            it.value()->store(true, std::memory_order_relaxed);
        }
    }

protected:
    // Moves `worker` onto this service's dedicated worker thread and starts it. Called once from
    // the derived constructor, after constructing its concrete worker.
    void startWorker(QObject* worker)
    {
        m_workerObject = worker;
        worker->moveToThread(&m_workerThread);
        m_workerThread.start();
    }

    // Registers a fresh cancel flag for `requestId`; call once per requestScan before dispatching
    // to the worker thread.
    std::shared_ptr<std::atomic<bool>> registerCancelFlag(int requestId)
    {
        auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
        QMutexLocker locker(&m_cancelFlagsMutex);
        m_cancelFlags.insert(requestId, cancelFlag);
        return cancelFlag;
    }

    void unregisterCancelFlag(int requestId)
    {
        QMutexLocker locker(&m_cancelFlagsMutex);
        m_cancelFlags.remove(requestId);
    }

    // Queues `func` to run on the worker thread -- a thin wrapper so derived services don't each
    // repeat the QMetaObject::invokeMethod(m_worker, ..., Qt::QueuedConnection) boilerplate.
    template <typename Func>
    void runOnWorker(Func&& func)
    {
        QMetaObject::invokeMethod(m_workerObject, std::forward<Func>(func), Qt::QueuedConnection);
    }

    // Bounces `callback(args...)` onto this service's thread (where `guard` lives), guarded so it
    // never fires after `guard` is destroyed. Used from worker-thread code for both progress and
    // finished callbacks; `args` are captured by value in a tuple so move-only results (e.g.
    // Result<T>) are carried across intact.
    template <typename Callback, typename... Args>
    void invokeGuarded(const QPointer<QObject>& guard, Callback callback, Args&&... args)
    {
        if (!callback)
        {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [guard, callback, argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                if (!guard)
                {
                    return;
                }
                std::apply(callback, std::move(argsTuple));
            },
            Qt::QueuedConnection);
    }

    QThread m_workerThread;

private:
    QObject* m_workerObject = nullptr;
    QMutex m_cancelFlagsMutex;
    QHash<int, std::shared_ptr<std::atomic<bool>>> m_cancelFlags; // guarded by m_cancelFlagsMutex
};
