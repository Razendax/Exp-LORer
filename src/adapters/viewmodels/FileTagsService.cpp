#include "FileTagsService.h"

#include <utility>

#include <QMetaObject>
#include <QPointer>

#include "FileTagsWorker.h"
#include "Logging.h"

FileTagsService::FileTagsService(IFileSystemRepository& fileSystemRepository, const std::filesystem::path& databasePath, QObject* parent)
    : QObject(parent)
{
    m_worker = new FileTagsWorker(fileSystemRepository, databasePath);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

FileTagsService::~FileTagsService()
{
    // m_worker's lazily-opened SQLiteTagRepository connection is thread-affine to the worker
    // thread, so it must be torn down there too -- same shape AdvancedSearchService already uses.
    m_worker->deleteLater();
    m_workerThread.quit();
    m_workerThread.wait();
}

void FileTagsService::requestTags(QObject* context, const std::vector<FileNode>& files,
                                   std::function<void(Result<TagsByPathMap>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, files, callback = std::move(callback)]() {
            Result<TagsByPathMap> result = m_worker->fetchTags(files);

            if (result.hasError())
            {
                Logging::warn("FileTagsService: fetchTags failed: " + std::string(toLogString(result.error().code)));
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
