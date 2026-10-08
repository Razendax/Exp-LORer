#pragma once

#include <filesystem>
#include <vector>

#include "DiskUsageNode.h"
#include "Result.h"

// Port for persisting/reading back disk-usage scan results (Architecture.md §14.39). Implemented
// by src/adapters/diskusage/SQLiteDiskUsageRepository.
class IDiskUsageRepository
{
public:
    virtual ~IDiskUsageRepository() = default;

    // Batched upsert of an entire scan's worth of nodes in one transaction (Architecture.md §14.39
    // -- "batch updates to minimize the number of database transactions"). Rows previously recorded
    // under scanRoot but absent from `nodes` (removed from disk since the last scan) are deleted, so
    // a later childrenOf() lookup never returns stale entries.
    virtual Result<void> saveScan(const std::filesystem::path& scanRoot, const std::vector<DiskUsageNode>& nodes) = 0;

    // Immediate children of `folder` from the most recent scan that covered it. Empty (not an
    // error) if `folder` was never scanned.
    virtual Result<std::vector<DiskUsageNode>> childrenOf(const std::filesystem::path& folder) const = 0;
};
