#pragma once

#include "IGitStatusProvider.h"

// Implements IGitStatusProvider via LibGit2Repository. Not a repository/connection cache -- opens
// (discovers) a repository fresh on every call, rooted at the queried directory; libgit2's
// git_repository_open_ext walk-upward discovery is cheap relative to the status scan itself.
class LibGit2StatusProvider : public IGitStatusProvider
{
public:
    Result<GitDirectoryStatus> statusForDirectory(const std::filesystem::path& directory) const override;
};
