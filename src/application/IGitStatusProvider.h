#pragma once

#include <filesystem>

#include "GitDirectoryStatus.h"
#include "Result.h"

// Port for computing one directory's immediate children's git working-tree status
// (Architecture.md §14.x). Real disk/.git I/O, so it's abstracted behind a Port like every other
// filesystem-adjacent capability, kept separate from IFileSystemRepository (Interface Segregation,
// same reasoning that already keeps IContextMenuProvider/IMediaDecoder as their own Ports).
// Implemented by src/adapters/git/LibGit2StatusProvider.
class IGitStatusProvider
{
public:
    virtual ~IGitStatusProvider() = default;

    // directory not inside any git repository => Result<GitDirectoryStatus>::success with
    // repositoryRoot=nullopt and an empty statusByPath, not an error (mirrors driveSpaceInfo's
    // "tolerate by blanking" posture). A genuine failure (corrupt repo, permission error) is
    // propagated as an Error.
    virtual Result<GitDirectoryStatus> statusForDirectory(const std::filesystem::path& directory) const = 0;
};
