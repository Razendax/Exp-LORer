#include "LibGit2BranchProvider.h"

#include <utility>

#include "LibGit2Repository.h"

Result<GitRepositorySummary> LibGit2BranchProvider::repositorySummary(const std::filesystem::path& directory) const
{
    auto repoResult = LibGit2Repository::open(directory);
    if (!repoResult)
    {
        // No repository at or above `directory` -- tolerated by blanking (same posture as
        // LibGit2StatusProvider::statusForDirectory), not an error.
        return Result<GitRepositorySummary>::success(GitRepositorySummary{});
    }

    LibGit2Repository repository = std::move(repoResult).value();
    const std::filesystem::path workdir = repository.workdirPath();
    if (workdir.empty())
    {
        // Bare repository -- no working tree/HEAD to show a summary for.
        return Result<GitRepositorySummary>::success(GitRepositorySummary{});
    }

    GitRepositorySummary summary;
    summary.repositoryRoot = workdir;

    bool isDetached = false;
    auto headResult = repository.headDisplayName(isDetached);
    if (!headResult)
    {
        return Result<GitRepositorySummary>::failure(std::move(headResult).error());
    }
    summary.headDisplayName = std::move(headResult).value();
    summary.isDetached = isDetached;

    auto changedCountResult = repository.changedFileCount();
    if (!changedCountResult)
    {
        return Result<GitRepositorySummary>::failure(std::move(changedCountResult).error());
    }
    summary.changedFileCount = changedCountResult.value();

    return Result<GitRepositorySummary>::success(std::move(summary));
}

Result<std::vector<std::string>> LibGit2BranchProvider::listLocalBranches(const std::filesystem::path& directory) const
{
    auto repoResult = LibGit2Repository::open(directory);
    if (!repoResult)
    {
        return Result<std::vector<std::string>>::failure(std::move(repoResult).error());
    }

    return std::move(repoResult).value().listLocalBranches();
}

Result<void> LibGit2BranchProvider::checkoutBranch(const std::filesystem::path& directory, const std::string& branchName) const
{
    auto repoResult = LibGit2Repository::open(directory);
    if (!repoResult)
    {
        return Result<void>::failure(std::move(repoResult).error());
    }

    return std::move(repoResult).value().checkoutBranch(branchName);
}
