#pragma once

#include <filesystem>

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

private:
    IGitStatusProvider& m_gitStatusProvider;
};
