#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

// Plain data: one set of byte-identical files discovered during a duplicate scan (Architecture.md
// §14.51). Application-layer like DiskUsageNode/GitFileStatus -- a derived scan result, not a
// fundamental per-file entity.
struct DuplicateFileGroup
{
    std::uint64_t contentHash = 0;
    std::uintmax_t fileSize = 0;
    std::vector<std::filesystem::path> paths;
};

inline bool operator==(const DuplicateFileGroup& lhs, const DuplicateFileGroup& rhs) noexcept
{
    return lhs.contentHash == rhs.contentHash && lhs.fileSize == rhs.fileSize && lhs.paths == rhs.paths;
}
