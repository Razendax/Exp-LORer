#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "DuplicateFileGroup.h"
#include "Result.h"

// Port for finding byte-identical file groups under a root (Architecture.md §14.51). Implemented by
// src/adapters/duplicates/StandardDuplicateFileScanner. Unlike IDiskUsageScanner, a scan result is
// never persisted -- its output drives a delete action, so a stale cached group would be a
// correctness hazard; every call re-reads the live filesystem.
class IDuplicateFileScanner
{
public:
    virtual ~IDuplicateFileScanner() = default;

    // onProgress reports hashing progress (files hashed so far / total candidate files queued for
    // hashing after the size-bucketing pre-filter) -- not a general directory-walk progress tick.
    // cancelRequested is polled at file boundaries (coarse granularity, same "not interruptible
    // mid-call" posture as WindowsDiskUsageScanner's walk); observing it set returns a Cancelled
    // error instead of partial groups.
    virtual Result<std::vector<DuplicateFileGroup>> scan(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress,
        const std::atomic<bool>& cancelRequested) const = 0;
};
