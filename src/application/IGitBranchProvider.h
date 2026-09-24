#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GitRepositorySummary.h"
#include "Result.h"

// Port for reading a repository's branch/change summary, listing local branches, and checking one
// out (Architecture.md §14.x, "Git branch indicator and checkout"). Split from IGitStatusProvider
// (Interface Segregation, same reasoning that already keeps IContextMenuProvider/IMediaDecoder as
// their own Ports) -- checkoutBranch is a write operation and IGitStatusProvider's existing callers
// (per-file status badges) are read-only. Implemented by src/adapters/git/LibGit2BranchProvider.
class IGitBranchProvider
{
public:
    virtual ~IGitBranchProvider() = default;

    // directory not inside any git repository => Result::success with repositoryRoot=nullopt and
    // the rest of the fields defaulted, not an error (mirrors IGitStatusProvider::statusForDirectory's
    // "tolerate by blanking" posture). A genuine failure (corrupt repo, permission error) is
    // propagated as an Error.
    virtual Result<GitRepositorySummary> repositorySummary(const std::filesystem::path& directory) const = 0;

    // Err(NotFound) if directory isn't inside a repository -- callers only invoke these after
    // repositorySummary() has confirmed one exists.
    virtual Result<std::vector<std::string>> listLocalBranches(const std::filesystem::path& directory) const = 0;
    virtual Result<void> checkoutBranch(const std::filesystem::path& directory, const std::string& branchName) const = 0;
};
