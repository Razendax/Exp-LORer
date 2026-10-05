#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "FileFragmentationInfo.h"
#include "Result.h"

// Port for scanning NTFS file fragmentation under a root (Architecture.md §14.52). Implemented by
// src/adapters/fragmentation/WindowsFragmentationScanner (Windows-only in v1; a non-Windows caller
// gets an IoError "Not supported on this platform" failure, same posture as
// IFileSystemRepository::showProperties). Mirrors IDuplicateFileScanner's shape exactly.
class IFragmentationScanner
{
public:
    virtual ~IFragmentationScanner() = default;

    // onProgress fires during both scan phases, distinguished by totalCandidates:
    //  - Listing phase (initial recursive enumeration): totalCandidates == 0 (total not yet
    //    known), scanned == files discovered so far, currentPath == the most recently discovered
    //    entry. A caller should treat this as an indeterminate-progress state.
    //  - Extent-counting phase (per candidate file >= minFileSizeBytes, after listing
    //    completes): totalCandidates == the final candidate count, scanned increments per file
    //    examined, currentPath == that candidate's own path.
    // cancelRequested is polled at file boundaries (coarse granularity, same "not interruptible
    // mid-call" posture as WindowsDiskUsageScanner/StandardDuplicateFileScanner); observing it set
    // returns a Cancelled error instead of a partial list.
    virtual Result<std::vector<FileFragmentationInfo>> scan(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)>&
            onProgress,
        const std::atomic<bool>& cancelRequested) const = 0;
};
