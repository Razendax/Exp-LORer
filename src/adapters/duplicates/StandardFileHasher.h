#pragma once

#include <cstdint>
#include <filesystem>

#include "Result.h"

// Streamed full-content xxHash64 of a file (Architecture.md §14.51) -- deliberately not
// IFileSystemRepository::computeFileHash, which mixes in mtime and only samples the first/last
// 64KB (a rename/move-detection heuristic, Architecture.md §8, not a content-equality check). A
// free function, not a class: no state to hold between calls.
namespace StandardFileHasher
{
    Result<std::uint64_t> hashFileContent(const std::filesystem::path& path);
}
