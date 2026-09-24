#pragma once

#include "IGitBranchProvider.h"

// Implements IGitBranchProvider via LibGit2Repository. Not a repository/connection cache -- opens
// (discovers) a repository fresh on every call, rooted at the queried directory, same posture as
// LibGit2StatusProvider.
class LibGit2BranchProvider : public IGitBranchProvider
{
public:
    Result<GitRepositorySummary> repositorySummary(const std::filesystem::path& directory) const override;
    Result<std::vector<std::string>> listLocalBranches(const std::filesystem::path& directory) const override;
    Result<void> checkoutBranch(const std::filesystem::path& directory, const std::string& branchName) const override;
};
