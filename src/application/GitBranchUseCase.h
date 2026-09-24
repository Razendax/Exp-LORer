#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GitRepositorySummary.h"
#include "Result.h"

class IGitBranchProvider;

// Reads a repository's branch/change summary, lists local branches, and checks one out
// (Architecture.md §14.x). Stateless, depends only on IGitBranchProvider& (same single-Port-
// dependency shape as GitStatusUseCase) -- a thin passthrough for now, leaving room to add
// filtering/caching later without touching the Port.
class GitBranchUseCase
{
public:
    explicit GitBranchUseCase(IGitBranchProvider& gitBranchProvider);

    Result<GitRepositorySummary> repositorySummary(const std::filesystem::path& directory) const;
    Result<std::vector<std::string>> listLocalBranches(const std::filesystem::path& directory) const;
    Result<void> checkoutBranch(const std::filesystem::path& directory, const std::string& branchName) const;

private:
    IGitBranchProvider& m_gitBranchProvider;
};
