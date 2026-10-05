#include "AdvancedSearchService.h"

#include <utility>

#include <QMetaObject>
#include <QPointer>

#include "AdvancedSearchWorker.h"
#include "Logging.h"

AdvancedSearchService::AdvancedSearchService(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                                              const std::filesystem::path& databasePath, QObject* parent)
    : QObject(parent)
{
    m_worker = new AdvancedSearchWorker(fileNavigationUseCase, fileSystemRepository, databasePath);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

AdvancedSearchService::~AdvancedSearchService()
{
    // m_worker's lazily-opened SQLiteTagRepository connection is thread-affine to the worker
    // thread, so it must be torn down there too -- deleteLater() (thread-safe to call from here)
    // queues the deletion on the worker thread's event loop, ahead of the quit() event below.
    m_worker->deleteLater();
    m_workerThread.quit();
    m_workerThread.wait();
}

void AdvancedSearchService::requestRecursiveScan(QObject* context, const std::filesystem::path& root,
                                                  std::function<void(Result<std::vector<FileNode>>)> callback,
                                                  std::function<void(std::vector<FileNode>)> onProgress, bool skipGitIgnored)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, root, callback = std::move(callback), onProgress = std::move(onProgress), skipGitIgnored]() {
            // Called synchronously on this worker thread from inside the scan below -- bounce each
            // tick back to the requesting side the same way the final result is, so onProgress
            // never runs on the wrong thread and never fires after `context` is gone.
            auto progressBridge = [this, guard, onProgress](const std::vector<FileNode>& partial) {
                if (!onProgress)
                {
                    return;
                }

                QMetaObject::invokeMethod(
                    this,
                    [guard, onProgress, partial]() {
                        if (!guard)
                        {
                            return;
                        }
                        onProgress(partial);
                    },
                    Qt::QueuedConnection);
            };

            Result<std::vector<FileNode>> result = m_worker->scanRecursive(root, progressBridge, skipGitIgnored);

            if (result.hasError())
            {
                Logging::warn("AdvancedSearchService: recursive scan failed: " + std::string(toLogString(result.error().code)));
            }
            else
            {
                Logging::debug("AdvancedSearchService: recursive scan finished (" + std::to_string(result.value().size()) +
                                " result(s))");
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

void AdvancedSearchService::requestTagFirstCandidates(QObject* context, const std::filesystem::path& root,
                                                       const std::vector<Tag::Id>& tagIds,
                                                       std::function<void(Result<std::vector<FileNode>>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, root, tagIds, callback = std::move(callback)]() {
            Result<std::vector<FileNode>> result = m_worker->fetchTagFirstCandidates(root, tagIds);

            if (result.hasError())
            {
                Logging::warn("AdvancedSearchService: tag-first candidates failed: " + std::string(toLogString(result.error().code)));
            }
            else
            {
                Logging::debug("AdvancedSearchService: tag-first candidates finished (" + std::to_string(result.value().size()) +
                                " result(s))");
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
