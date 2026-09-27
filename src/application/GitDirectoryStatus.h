#pragma once
#include <filesystem>
#include <optional>
#include <unordered_map>

#include "GitFileStatus.h"
#include "PathHash.h"

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
