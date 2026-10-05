#pragma once

#include <cstdint>
#include <filesystem>

// Plain data describing one file's NTFS fragmentation state (Architecture.md §14.52). Application-
// layer like DuplicateFileGroup/DiskUsageNode -- a derived scan result, not a fundamental per-file
// entity. fragmentCount <= 1 is contiguous; a file with no allocated extents (resident entirely in
// its MFT record) is reported as 1, not 0 -- that's the most-contiguous case, not an unknown one.
struct FileFragmentationInfo
{
    std::filesystem::path path;
    std::uintmax_t sizeBytes = 0;
    std::uint32_t fragmentCount = 0;
};

inline bool operator==(const FileFragmentationInfo& lhs, const FileFragmentationInfo& rhs) noexcept
{
    return lhs.path == rhs.path && lhs.sizeBytes == rhs.sizeBytes && lhs.fragmentCount == rhs.fragmentCount;
}
