#include "GitBranchUseCase.h"

#include "IGitBranchProvider.h"

GitBranchUseCase::GitBranchUseCase(IGitBranchProvider& gitBranchProvider)
    : m_gitBranchProvider(gitBranchProvider)
{
}

Result<GitRepositorySummary> GitBranchUseCase::repositorySummary(const std::filesystem::path& directory) const
{
    return m_gitBranchProvider.repositorySummary(directory);
}

Result<std::vector<std::string>> GitBranchUseCase::listLocalBranches(const std::filesystem::path& directory) const
{
    return m_gitBranchProvider.listLocalBranches(directory);
}

Result<void> GitBranchUseCase::checkoutBranch(const std::filesystem::path& directory, const std::string& branchName) const
{
    return m_gitBranchProvider.checkoutBranch(directory, branchName);
}
