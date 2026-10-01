#pragma once

#include <filesystem>
#include <vector>

#include "GitDirectoryStatus.h"
#include "Result.h"

class IGitStatusProvider;

// Computes git working-tree status for one directory's immediate children (Architecture.md
// §14.x). Stateless, depends only on IGitStatusProvider& (same single-Port-dependency shape as
// FilePreviewUseCase) -- a thin passthrough for now, leaving room to add filtering/caching later
// without touching the Port.
class GitStatusUseCase
{
public:
    explicit GitStatusUseCase(IGitStatusProvider& gitStatusProvider);

    Result<GitDirectoryStatus> statusForDirectory(const std::filesystem::path& directory) const;

    // Thin passthrough to IGitStatusProvider::ignoredPaths (Architecture.md §14.45).
    Result<std::vector<std::filesystem::path>> ignoredPaths(const std::filesystem::path& root) const;

private:
    IGitStatusProvider& m_gitStatusProvider;
};
