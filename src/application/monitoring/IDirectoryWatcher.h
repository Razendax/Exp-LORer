#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

#include "Result.h"

using DirectoryWatchId = std::uint64_t;

// Port for watching one directory's immediate children for create/delete/rename/modify changes
// (Architecture.md §14.x), kept separate from IFileSystemRepository (Interface Segregation, same
// reasoning that already keeps IGitStatusProvider/IContextMenuProvider/IMediaDecoder as their own
// Ports). Implemented by src/adapters/filesystem/StandardDirectoryWatcher.
class IDirectoryWatcher
{
public:
    virtual ~IDirectoryWatcher() = default;

    // Registers onChanged to fire whenever `directory`'s immediate children change (create/
    // delete/rename/modify) -- never for anything inside a nested subdirectory. Multiple
    // independent watches on the same directory are allowed. Result carries a failure for a
    // non-real path (the "This PC" sentinel, an archive-virtual path, a path that doesn't exist).
    virtual Result<DirectoryWatchId> watch(const std::filesystem::path& directory, std::function<void()> onChanged) = 0;

    // No-op if id is unknown/already removed.
    virtual void unwatch(DirectoryWatchId id) = 0;
};
