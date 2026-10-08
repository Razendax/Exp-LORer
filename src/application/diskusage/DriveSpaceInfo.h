#pragma once
#include <cstdint>
#include <string>

// Plain data crossing the Application boundary — displayLabel is already fully resolved (UTF-8,
// e.g. "Local Disk (C:)") by the adapter via the same DriveLabel helper StandardFileSystemRepository
// already uses for "This PC" (Architecture.md §14.15), so no OS-specific type (drive letter,
// GetDriveTypeW code) leaks into Application. Mirrors FileNode::displayName()'s posture.
struct DriveSpaceInfo
{
    std::string displayLabel;
    std::uint64_t freeBytes = 0;
    std::uint64_t totalBytes = 0;
};
