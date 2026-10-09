#include "IconLoadingService.h"

#include <algorithm>
#include <utility>

#include <QMetaObject>
#include <QMutexLocker>
#include <QPixmap>
#include <QPointer>
#include <QThread>

#include "IconLoadingWorker.h"

namespace
{
    constexpr std::size_t kChunkSize = 20;

    // Clamped to [2, 4]: enough to actually parallelize icon extraction, while leaving headroom
    // for the UI thread and every other service's own single worker thread (see
    // IconLoadingService.h's class comment for why this one deviates from that convention).
    std::size_t poolSize()
    {
        return static_cast<std::size_t>(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
    }
}

IconLoadingService::IconLoadingService(QObject* parent)
    : QObject(parent)
{
    const std::size_t count = poolSize();
    m_pool.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        PoolWorker entry;
        entry.worker = new IconLoadingWorker();
        entry.thread = new QThread();
        entry.worker->moveToThread(entry.thread);
        entry.thread->start();
        m_pool.push_back(entry);
    }
}

IconLoadingService::~IconLoadingService()
{
    for (PoolWorker& entry : m_pool)
    {
        entry.thread->quit();
        entry.thread->wait();
        delete entry.worker;
        delete entry.thread;
    }
}

void IconLoadingService::requestIcons(QObject* context, const std::filesystem::path& directory, std::vector<FileNode> entries,
                                       IconSizeMask sizeMask, std::function<void(QHash<QString, QIcon>)> onBatch)
{
    {
        QMutexLocker locker(&m_activeDirectoryMutex);
        m_activeDirectoryByContext.insert(context, directory);
    }

    QPointer<QObject> guard(context);

    for (std::size_t offset = 0; offset < entries.size(); offset += kChunkSize)
    {
        const std::size_t count = std::min(kChunkSize, entries.size() - offset);
        std::vector<FileNode> chunk(entries.begin() + static_cast<std::ptrdiff_t>(offset),
                                     entries.begin() + static_cast<std::ptrdiff_t>(offset + count));

        const std::size_t workerIndex = m_nextWorkerIndex.fetch_add(1, std::memory_order_relaxed) % m_pool.size();
        IconLoadingWorker* worker = m_pool[workerIndex].worker;

        // Both hops are queued functor calls (QMetaObject::invokeMethod), not Qt signals/slots --
        // QHash<QString, QIcon>/std::vector<FileNode> aren't (and shouldn't need to become)
        // registered Qt meta-types, matching GitStatusService's own reasoning.
        QMetaObject::invokeMethod(
            worker,
            [guard, context, directory, sizeMask, chunk = std::move(chunk), onBatch, worker, this]() {
                {
                    QMutexLocker locker(&m_activeDirectoryMutex);
                    const auto activeIt = m_activeDirectoryByContext.constFind(context);
                    if (activeIt == m_activeDirectoryByContext.constEnd() || activeIt.value() != directory)
                    {
                        // Superseded by a newer request from this same context -- drop this
                        // not-yet-started chunk rather than spend time resolving icons nobody will
                        // see.
                        return;
                    }
                }

                QHash<QString, IconImages> images = worker->iconsFor(chunk, sizeMask);

                QMetaObject::invokeMethod(
                    this,
                    [guard, onBatch, images = std::move(images)]() {
                        if (!guard)
                        {
                            return;
                        }

                        // QPixmap/QIcon construction must happen on the GUI thread -- images was
                        // carried across as plain QImage data (no GUI-thread affinity) for exactly
                        // this reason.
                        QHash<QString, QIcon> batch;
                        batch.reserve(images.size());
                        for (auto it = images.constBegin(); it != images.constEnd(); ++it)
                        {
                            QIcon icon;
                            if (!it.value().smallImage.isNull())
                            {
                                icon.addPixmap(QPixmap::fromImage(it.value().smallImage));
                            }
                            if (!it.value().largeImage.isNull())
                            {
                                icon.addPixmap(QPixmap::fromImage(it.value().largeImage));
                            }
                            batch.insert(it.key(), icon);
                        }

                        onBatch(batch);
                    },
                    Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    }
}
