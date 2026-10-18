#pragma once

#include <atomic>
#include <memory>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>

#include "WorkerThreadService.h"

// Adds per-request cancellation, identified by a caller-supplied requestId (the NotificationCenter
// ProgressId it already created), on top of WorkerThreadService's single-worker-thread/guarded-
// callback plumbing. Shared by the per-feature scan services (DiskUsageService,
// DuplicateFinderService, FragmentationService, Architecture.md §14.70).
class CancellableScanService : public WorkerThreadService
{
    Q_OBJECT

public:
    explicit CancellableScanService(QObject* parent = nullptr)
        : WorkerThreadService(parent)
    {
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

private:
    QMutex m_cancelFlagsMutex;
    QHash<int, std::shared_ptr<std::atomic<bool>>> m_cancelFlags; // guarded by m_cancelFlagsMutex
};
