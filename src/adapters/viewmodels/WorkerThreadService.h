#pragma once

#include <tuple>
#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>

// Shared single-dedicated-worker-thread + guarded-callback-bouncing plumbing behind every
// *Service in this directory that owns exactly one background QThread (GitStatusService,
// AdvancedSearchService, FileTransferService, FileTagsService, FilePreviewViewModel, and --
// via CancellableScanService, which adds per-request cancellation on top -- DiskUsageService/
// DuplicateFinderService/FragmentationService). A request is dispatched to the worker thread via
// runOnWorker(); any callback invoked from worker-thread code is bounced back to this service's
// own thread via invokeGuarded(), guarded by a QPointer<QObject> so it never fires after the
// caller-supplied `context` has been destroyed.
class WorkerThreadService : public QObject
{
    Q_OBJECT

public:
    explicit WorkerThreadService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~WorkerThreadService() override
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

protected:
    // Moves `worker` onto this service's dedicated worker thread and starts it. Called once from
    // the derived constructor, after constructing its concrete worker.
    void startWorker(QObject* worker)
    {
        m_workerObject = worker;
        worker->moveToThread(&m_workerThread);
        m_workerThread.start();
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
};
