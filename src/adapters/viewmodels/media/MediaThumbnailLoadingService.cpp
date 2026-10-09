#include "MediaThumbnailLoadingService.h"

#include <algorithm>
#include <utility>

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>

#include "MediaThumbnailLoadingWorker.h"

namespace
{
    constexpr std::size_t kChunkSize = 10;
}

MediaThumbnailLoadingService::MediaThumbnailLoadingService(MediaProcessingUseCase& mediaProcessingUseCase, QObject* parent)
    : QObject(parent)
{
    m_worker = new MediaThumbnailLoadingWorker(mediaProcessingUseCase);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

MediaThumbnailLoadingService::~MediaThumbnailLoadingService()
{
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
}

void MediaThumbnailLoadingService::requestThumbnails(QObject* context, const std::filesystem::path& root,
                                                       std::vector<FileNode> entries, int maxWidth, int maxHeight,
                                                       std::function<void(QHash<QString, MediaThumbnailData>)> onBatch)
{
    {
        QMutexLocker locker(&m_activeRootMutex);
        m_activeRootByContext.insert(context, root);
    }

    QPointer<QObject> guard(context);

    for (std::size_t offset = 0; offset < entries.size(); offset += kChunkSize)
    {
        const std::size_t count = std::min(kChunkSize, entries.size() - offset);
        std::vector<FileNode> chunk(entries.begin() + static_cast<std::ptrdiff_t>(offset),
                                     entries.begin() + static_cast<std::ptrdiff_t>(offset + count));

        // Both hops are queued functor calls (QMetaObject::invokeMethod), not Qt signals/slots --
        // QHash<QString, MediaThumbnailData>/std::vector<FileNode> aren't (and shouldn't need to
        // become) registered Qt meta-types, matching IconLoadingService's own reasoning.
        QMetaObject::invokeMethod(
            m_worker,
            [this, guard, context, root, chunk = std::move(chunk), maxWidth, maxHeight, onBatch]() {
                {
                    QMutexLocker locker(&m_activeRootMutex);
                    const auto activeIt = m_activeRootByContext.constFind(context);
                    if (activeIt == m_activeRootByContext.constEnd() || activeIt.value() != root)
                    {
                        // Superseded by a newer request from this same context -- drop this
                        // not-yet-started chunk rather than spend time decoding thumbnails nobody
                        // will see.
                        return;
                    }
                }

                QHash<QString, MediaThumbnailData> batch = m_worker->thumbnailsFor(chunk, maxWidth, maxHeight);

                QMetaObject::invokeMethod(
                    this,
                    [guard, onBatch, batch = std::move(batch)]() {
                        if (!guard)
                        {
                            return;
                        }
                        onBatch(batch);
                    },
                    Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    }
}
