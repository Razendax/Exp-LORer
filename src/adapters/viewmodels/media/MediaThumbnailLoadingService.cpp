#include "MediaThumbnailLoadingService.h"

#include <algorithm>
#include <utility>

#include <QMetaObject>
#include <QMutexLocker>

#include "MediaThumbnailLoadingWorker.h"

namespace
{
    constexpr std::size_t kChunkSize = 10;
}

MediaThumbnailLoadingService::MediaThumbnailLoadingService(MediaProcessingUseCase& mediaProcessingUseCase, QObject* parent)
    : QObject(parent)
    , m_pool([&mediaProcessingUseCase] { return new MediaThumbnailLoadingWorker(mediaProcessingUseCase); })
{
    for (std::size_t i = 0; i < m_pool.size(); ++i)
    {
        m_idleWorkers.push_back(i);
    }
}

MediaThumbnailLoadingService::~MediaThumbnailLoadingService() = default;

void MediaThumbnailLoadingService::requestThumbnails(QObject* context, const std::filesystem::path& root,
                                                       std::vector<FileNode> entries, int maxWidth, int maxHeight,
                                                       std::function<void(QHash<QString, MediaThumbnailData>)> onBatch,
                                                       Priority priority)
{
    {
        QMutexLocker locker(&m_activeRootMutex);
        m_activeRootByContext.insert(context, root);
    }

    QPointer<QObject> guard(context);

    std::vector<PendingChunk> newChunks;
    for (std::size_t offset = 0; offset < entries.size(); offset += kChunkSize)
    {
        const std::size_t count = std::min(kChunkSize, entries.size() - offset);
        std::vector<FileNode> chunk(entries.begin() + static_cast<std::ptrdiff_t>(offset),
                                     entries.begin() + static_cast<std::ptrdiff_t>(offset + count));

        PendingChunk pending;
        pending.context = context;
        pending.guard = guard;
        pending.root = root;
        pending.chunk = std::move(chunk);
        pending.maxWidth = maxWidth;
        pending.maxHeight = maxHeight;
        pending.onBatch = onBatch;
        pending.priority = priority;
        newChunks.push_back(std::move(pending));
    }

    {
        QMutexLocker locker(&m_pendingMutex);
        if (priority == Priority::High)
        {
            // A newer filter value supersedes this context's previous one -- drop its still-queued
            // (not yet started) High-priority chunks. Low-priority chunks (a kind's background
            // sweep) are left untouched, so that sweep's eventual full-kind coverage is never
            // discarded, only out-prioritized.
            m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                            [context](const PendingChunk& pending) {
                                                return pending.context == context && pending.priority == Priority::High;
                                            }),
                             m_pending.end());
            m_pending.insert(m_pending.begin(), newChunks.begin(), newChunks.end());
        }
        else
        {
            m_pending.insert(m_pending.end(), newChunks.begin(), newChunks.end());
        }
    }

    startNextIfIdle();
}

void MediaThumbnailLoadingService::startNextIfIdle()
{
    while (true)
    {
        std::optional<PendingChunk> next;
        std::size_t workerIndex = 0;
        {
            QMutexLocker locker(&m_pendingMutex);
            if (m_pending.empty() || m_idleWorkers.empty())
            {
                return;
            }
            next = std::move(m_pending.front());
            m_pending.pop_front();
            workerIndex = m_idleWorkers.front();
            m_idleWorkers.pop_front();
        }

        MediaThumbnailLoadingWorker* worker = m_pool.workerAt(workerIndex);

        QMetaObject::invokeMethod(
            worker,
            [this, next = std::move(*next), worker, workerIndex]() mutable {
                bool stale = false;
                {
                    QMutexLocker locker(&m_activeRootMutex);
                    const auto activeIt = m_activeRootByContext.constFind(next.context);
                    stale = activeIt == m_activeRootByContext.constEnd() || activeIt.value() != next.root;
                }

                QHash<QString, MediaThumbnailData> batch;
                if (!stale)
                {
                    // Superseded by a newer request from this same context -- skip decoding
                    // thumbnails nobody will see, but still deliver an (empty) completion so the
                    // pump advances.
                    batch = worker->thumbnailsFor(next.chunk, next.maxWidth, next.maxHeight);
                }

                QMetaObject::invokeMethod(
                    this,
                    [this, guard = next.guard, onBatch = next.onBatch, batch = std::move(batch), workerIndex]() {
                        {
                            QMutexLocker locker(&m_pendingMutex);
                            m_idleWorkers.push_back(workerIndex);
                        }
                        if (guard)
                        {
                            onBatch(batch);
                        }
                        startNextIfIdle();
                    },
                    Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    }
}
