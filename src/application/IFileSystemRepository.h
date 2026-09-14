#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "FileNode.h"
#include "Result.h"

// Port for OS file operations (Architecture.md §2.2). Implemented by
// src/adapters/filesystem/StandardFileSystemRepository. No Qt/std::filesystem-specific
// behavior leaks past this interface into the Application layer.
class IFileSystemRepository
{
public:
    virtual ~IFileSystemRepository() = default;

    virtual Result<std::vector<FileNode>> listDirectory(const std::filesystem::path& directory) const = 0;

    // Resolves a single path into a FileNode (e.g. to treat a browsed directory itself as a
    // taggable target when no child row is selected). Unlike listDirectory(), this stats the
    // path itself rather than enumerating its children.
    virtual Result<FileNode> stat(const std::filesystem::path& path) const = 0;

    virtual Result<FileNode> move(const std::filesystem::path& source, const std::filesystem::path& destination) = 0;
    virtual Result<FileNode> copy(const std::filesystem::path& source, const std::filesystem::path& destination) = 0;

    // Moves to the OS trash where available; see moveToTrash vs. deletePermanently distinction in Architecture.md §9.
    virtual Result<void> moveToTrash(const std::filesystem::path& path) = 0;
    virtual Result<void> deletePermanently(const std::filesystem::path& path) = 0;

    // Fast partial hash (size + first/last 64KB + mtime via xxHash64) per Architecture.md §8 — a
    // rename/move detection heuristic, not a cryptographic or uniqueness guarantee.
    virtual Result<std::uint64_t> computeFileHash(const FileNode& file) const = 0;
};
