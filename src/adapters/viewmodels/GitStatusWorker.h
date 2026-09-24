#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <QObject>

#include "GitDirectoryStatus.h"
#include "GitRepositorySummary.h"
#include "Result.h"

class GitStatusUseCase;
class GitBranchUseCase;

// Runs GitStatusUseCase::statusForDirectory and GitBranchUseCase's methods on whatever thread it
// has been moved to (Architecture.md §5/§14.x) -- libgit2 status/branch/checkout calls are disk/
// .git I/O and must never run on the Qt UI thread. Owned and driven by GitStatusService, same
// shape as FilePreviewViewModel/FilePreviewWorker.
class GitStatusWorker : public QObject
{
    Q_OBJECT

public:
    explicit GitStatusWorker(GitStatusUseCase& gitStatusUseCase, GitBranchUseCase& gitBranchUseCase, QObject* parent = nullptr);

    Result<GitDirectoryStatus> compute(const std::filesystem::path& directory) const;

    Result<GitRepositorySummary> computeRepositorySummary(const std::filesystem::path& directory) const;
    Result<std::vector<std::string>> computeLocalBranches(const std::filesystem::path& directory) const;
    Result<void> computeCheckout(const std::filesystem::path& directory, const std::string& branchName) const;

private:
    GitStatusUseCase& m_gitStatusUseCase;
    GitBranchUseCase& m_gitBranchUseCase;
};
