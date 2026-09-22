#pragma once
#include <cstddef>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include "GitFileStatus.h"

// std::filesystem::path has no std::hash specialization (unlike std::string) -- only the free
// std::filesystem::hash_value() function, the same helper StandardFileSystemRepository already
// uses for path-keyed lookups -- so an unordered_map keyed by path needs this explicit hasher.
struct PathHash
{
    std::size_t operator()(const std::filesystem::path& path) const noexcept { return std::filesystem::hash_value(path); }
};

using GitStatusByPathMap = std::unordered_map<std::filesystem::path, GitFileStatus, PathHash>;

// Plain data crossing the Application boundary -- one directory's immediate children's git status
// (Architecture.md §14.x). repositoryRoot is nullopt when the queried directory isn't inside any
// git repository, in which case statusByPath is always empty (same "plain Port-adjacent data type"
// posture as DriveSpaceInfo.h).
struct GitDirectoryStatus
{
    std::optional<std::filesystem::path> repositoryRoot;
    GitStatusByPathMap statusByPath;
};
