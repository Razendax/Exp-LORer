#include "GitStatusService.h"

#include <utility>

#include <QMetaObject>
#include <QPointer>

#include "GitStatusWorker.h"

GitStatusService::GitStatusService(GitStatusUseCase& gitStatusUseCase, QObject* parent)
    : QObject(parent)
{
    m_worker = new GitStatusWorker(gitStatusUseCase);
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
