#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include "DiskUsageNode.h"
#include "Result.h"

// Port for recursively computing per-file sizes and per-directory recursive aggregate sizes under
// a root (Architecture.md §14.39, "Disk Usage / Folder Size"). Implemented by
// src/adapters/diskusage/WindowsDiskUsageScanner (Windows-only in v1; a non-Windows caller gets an
// IoError "Not supported on this platform" failure, same posture as
// IFileSystemRepository::showProperties).
class IDiskUsageScanner
{
public:
    virtual ~IDiskUsageScanner() = default;

    // Returns a flat list covering root and every descendant (root itself included), each
    // directory's size already the recursive aggregate of its own subtree. onProgress fires
    // throttled to ~once/sec during the walk with a running discovered-count and the entry just
    // visited -- this scanner has no separate "count candidates first" pass (unlike
    // IFragmentationScanner's two-phase scan), so it is always the indeterminate case, never a
    // known total. cancelRequested is polled at each directory entry (coarse granularity, same
    // "not interruptible mid-call" posture as IFragmentationScanner); observing it set returns a
    // Cancelled error instead of a partial list.
    virtual Result<std::vector<DiskUsageNode>> scan(
        const std::filesystem::path& root,
        const std::function<void(std::size_t scanned, const std::filesystem::path& currentPath)>& onProgress,
        const std::atomic<bool>& cancelRequested) const = 0;
};
