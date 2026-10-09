#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <QObject>
#include <QThread>

#include "GitDirectoryStatus.h"
#include "GitRepositorySummary.h"
#include "Result.h"

class GitStatusUseCase;
class GitBranchUseCase;
class GitStatusWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as FilePreviewViewModel). Runs every open tab/pane's git-status and
// git-branch requests through one dedicated background QThread, serialized (one always-alive
// worker, not a pool), sidestepping any libgit2 thread-safety question by construction
// (Architecture.md §14.x) -- branch/checkout requests deliberately share this same thread rather
// than getting one of their own.
class GitStatusService : public QObject
{
    Q_OBJECT

public:
    explicit GitStatusService(GitStatusUseCase& gitStatusUseCase, GitBranchUseCase& gitBranchUseCase, QObject* parent = nullptr);
    ~GitStatusService() override;

    // Dispatches a background statusForDirectory(directory) call and marshals the result back to
    // the calling thread via `callback`. `context` is watched via QPointer for destruction: if it
    // has been destroyed by the time the reply comes back (e.g. a tab was closed mid-request), the
    // callback is silently skipped -- the manual equivalent of Qt's connect(..., context, ...)
    // auto-disconnect-on-destruction idiom, needed here since this uses invokeMethod+functor rather
    // than a real signal/slot connection (GitDirectoryStatus/Result<T> aren't Qt meta-types).
    void requestStatus(QObject* context, const std::filesystem::path& directory,
                        std::function<void(Result<GitDirectoryStatus>)> callback);

    // Same dispatch/guard shape as requestStatus, for the branch/change summary, the local-branch
    // list (fetched on demand only, when the branch button is clicked), and a checkout.
    void requestRepositorySummary(QObject* context, const std::filesystem::path& directory,
                                   std::function<void(Result<GitRepositorySummary>)> callback);
    void requestLocalBranches(QObject* context, const std::filesystem::path& directory,
                               std::function<void(Result<std::vector<std::string>>)> callback);
    void requestCheckout(QObject* context, const std::filesystem::path& directory, const std::string& branchName,
                          std::function<void(Result<void>)> callback);

private:
    QThread m_workerThread;
    GitStatusWorker* m_worker = nullptr;
};
