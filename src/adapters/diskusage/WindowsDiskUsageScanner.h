#pragma once

#include "IDiskUsageScanner.h"

// Implements IDiskUsageScanner via FindFirstFileW/FindNextFileW (Architecture.md §14.39) rather
// than std::filesystem::recursive_directory_iterator -- sizes/attributes come straight off
// WIN32_FIND_DATAW with no extra per-entry stat() call, the actual efficiency win for this bulk
// size-aggregation workload. Windows-only; scan() fails with an IoError "Not supported on this
// platform" everywhere else, same posture as StandardFileSystemRepository::showProperties.
class WindowsDiskUsageScanner : public IDiskUsageScanner
{
public:
    Result<std::vector<DiskUsageNode>> scan(
        const std::filesystem::path& root,
        const std::function<void(std::size_t scanned, const std::filesystem::path& currentPath)>& onProgress,
        const std::atomic<bool>& cancelRequested) const override;
};
