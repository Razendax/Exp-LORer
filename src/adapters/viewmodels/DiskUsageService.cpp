#include "DiskUsageService.h"

#include <utility>

#include <QMetaObject>
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

void DiskUsageService::requestScan(QObject* context, const std::filesystem::path& root, std::function<void(Result<void>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, root, callback = std::move(callback)]() {
            Result<void> result = m_worker->scanAndPersist(root);

            if (result.hasError())
            {
                Logging::warn("DiskUsageService: scan failed: " + std::string(toLogString(result.error().code)));
            }
            else
            {
                Logging::debug("DiskUsageService: scan finished");
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
