#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include <QThread>

// Fixed-size pool of `WorkerT` instances, each moved onto its own dedicated QThread and torn down
// (quit/wait/delete) together. Shared by IconLoadingService and MediaThumbnailLoadingService
// (Architecture.md §14.78) -- both deliberately deviate from every other *Service's single
// dedicated worker thread because their per-item work (icon extraction / video decode) is
// expensive enough to benefit from running chunks concurrently. Dispatch strategy (which worker
// gets which chunk, queueing/priority) stays with each service -- only pool construction/teardown
// and sizing are shared here.
template <typename WorkerT>
class WorkerThreadPool
{
public:
    // `makeWorker` is called once per pool slot, on the thread constructing the pool, to create
    // that slot's WorkerT* (ownership passes to the pool); it's then moved onto a freshly started
    // dedicated QThread.
    template <typename MakeWorker>
    explicit WorkerThreadPool(MakeWorker makeWorker)
    {
        const std::size_t count = poolSize();
        m_entries.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            Entry entry;
            entry.worker = makeWorker();
            entry.thread = new QThread();
            entry.worker->moveToThread(entry.thread);
            entry.thread->start();
            m_entries.push_back(entry);
        }
    }

    ~WorkerThreadPool()
    {
        for (Entry& entry : m_entries)
        {
            entry.thread->quit();
            entry.thread->wait();
            delete entry.worker;
            delete entry.thread;
        }
    }

    WorkerThreadPool(const WorkerThreadPool&) = delete;
    WorkerThreadPool& operator=(const WorkerThreadPool&) = delete;

    std::size_t size() const
    {
        return m_entries.size();
    }

    WorkerT* workerAt(std::size_t index) const
    {
        return m_entries[index].worker;
    }

    // Clamped to [2, 4]: enough to actually parallelize per-item work, while leaving headroom for
    // the UI thread and every other service's own single worker thread.
    static std::size_t poolSize()
    {
        return static_cast<std::size_t>(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
    }

private:
    struct Entry
    {
        QThread* thread = nullptr;
        WorkerT* worker = nullptr;
    };

    std::vector<Entry> m_entries;
};
