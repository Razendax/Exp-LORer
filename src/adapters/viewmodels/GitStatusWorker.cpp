#include "GitStatusWorker.h"

#include "GitBranchUseCase.h"
#include "GitStatusUseCase.h"

GitStatusWorker::GitStatusWorker(GitStatusUseCase& gitStatusUseCase, GitBranchUseCase& gitBranchUseCase, QObject* parent)
    : QObject(parent)
    , m_gitStatusUseCase(gitStatusUseCase)
    , m_gitBranchUseCase(gitBranchUseCase)
{
}

Result<GitDirectoryStatus> GitStatusWorker::compute(const std::filesystem::path& directory) const
{
    return m_gitStatusUseCase.statusForDirectory(directory);
}

Result<GitRepositorySummary> GitStatusWorker::computeRepositorySummary(const std::filesystem::path& directory) const
{
    return m_gitBranchUseCase.repositorySummary(directory);
}

Result<std::vector<std::string>> GitStatusWorker::computeLocalBranches(const std::filesystem::path& directory) const
{
    return m_gitBranchUseCase.listLocalBranches(directory);
}

Result<void> GitStatusWorker::computeCheckout(const std::filesystem::path& directory, const std::string& branchName) const
{
    return m_gitBranchUseCase.checkoutBranch(directory, branchName);
}
