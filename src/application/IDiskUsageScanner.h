#pragma once

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
    // directory's size already the recursive aggregate of its own subtree. onProgress is a
    // lightweight periodic tick with no partial data (v1 has no progress bar, only "Scanning..."
    // button text) -- mirrors the throttling style of
    // IFileSystemRepository::listDirectoryRecursive's onProgress, minus the payload.
    virtual Result<std::vector<DiskUsageNode>> scan(const std::filesystem::path& root,
                                                      const std::function<void()>& onProgress = nullptr) const = 0;
};
