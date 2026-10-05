#include "GitStatusService.h"

#include <utility>

#include <QMetaObject>
#include <QPointer>

#include "GitStatusWorker.h"
#include "Logging.h"

GitStatusService::GitStatusService(GitStatusUseCase& gitStatusUseCase, GitBranchUseCase& gitBranchUseCase, QObject* parent)
    : QObject(parent)
{
    m_worker = new GitStatusWorker(gitStatusUseCase, gitBranchUseCase);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

GitStatusService::~GitStatusService()
{
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
}

void GitStatusService::requestStatus(QObject* context, const std::filesystem::path& directory,
                                      std::function<void(Result<GitDirectoryStatus>)> callback)
{
    QPointer<QObject> guard(context);

    // Both hops are queued functor calls (QMetaObject::invokeMethod), not Qt signals/slots --
    // GitDirectoryStatus/Result aren't (and shouldn't need to become) registered Qt meta-types,
    // matching FilePreviewViewModel::dispatchGeneration's own reasoning.
    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, directory, callback = std::move(callback)]() {
            Result<GitDirectoryStatus> result = m_worker->compute(directory);
            if (result.hasError())
            {
                Logging::debug("GitStatusService: status computation failed: " + std::string(toLogString(result.error().code)));
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

void GitStatusService::requestRepositorySummary(QObject* context, const std::filesystem::path& directory,
                                                  std::function<void(Result<GitRepositorySummary>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, directory, callback = std::move(callback)]() {
            Result<GitRepositorySummary> result = m_worker->computeRepositorySummary(directory);

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

void GitStatusService::requestLocalBranches(QObject* context, const std::filesystem::path& directory,
                                             std::function<void(Result<std::vector<std::string>>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, directory, callback = std::move(callback)]() {
            Result<std::vector<std::string>> result = m_worker->computeLocalBranches(directory);

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

void GitStatusService::requestCheckout(QObject* context, const std::filesystem::path& directory, const std::string& branchName,
                                        std::function<void(Result<void>)> callback)
{
    QPointer<QObject> guard(context);

    QMetaObject::invokeMethod(
        m_worker,
        [this, guard, directory, branchName, callback = std::move(callback)]() {
            Result<void> result = m_worker->computeCheckout(directory, branchName);
            // Never log branchName -- Architecture.md §14.23 explicitly calls this out.
            if (result.hasError())
            {
                Logging::warn("GitStatusService: checkout failed: " + std::string(toLogString(result.error().code)));
            }
            else
            {
                Logging::info("GitStatusService: checkout succeeded");
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
