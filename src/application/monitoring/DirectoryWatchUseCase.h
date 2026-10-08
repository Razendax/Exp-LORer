#pragma once

#include <filesystem>
#include <functional>

#include "IDirectoryWatcher.h"
#include "Result.h"

// Watches a directory's immediate children for changes (Architecture.md §14.x). Stateless,
// depends only on IDirectoryWatcher& (same single-Port-dependency shape as GitStatusUseCase) -- a
// thin passthrough for now, leaving room to add filtering/coalescing later without touching the
// Port.
class DirectoryWatchUseCase
{
public:
    explicit DirectoryWatchUseCase(IDirectoryWatcher& directoryWatcher);

    Result<DirectoryWatchId> watch(const std::filesystem::path& directory, std::function<void()> onChanged);
    void unwatch(DirectoryWatchId id);

private:
    IDirectoryWatcher& m_directoryWatcher;
};
