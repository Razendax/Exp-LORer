#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "DiskUsageNode.h"
#include "IDiskUsageRepository.h"
#include "IDiskUsageScanner.h"
#include "Result.h"

// Orchestrates disk-usage scanning + persistence (Architecture.md §14.39). Stateless, same posture
// as the rest of §2.2 -- a single shared instance may serve concurrent callers.
class DiskUsageUseCase
{
public:
    DiskUsageUseCase(IDiskUsageScanner& scanner, IDiskUsageRepository& repository);

    // Scans root, persists the result, and returns the full flat node list (root and every
    // descendant) as the scanner produced it.
    Result<std::vector<DiskUsageNode>> scanAndPersist(const std::filesystem::path& root);

    // Pure, no I/O: sorts descending by size and truncates to `max`. Percentage basis is the
    // caller's concern -- each shown segment's percentage should be computed against the *full*
    // children total (Architecture.md §14.39), not the sum of the shown top-N, so callers must sum
    // the pre-truncation list themselves before calling this.
    static std::vector<DiskUsageNode> topChildren(std::vector<DiskUsageNode> children, std::size_t max = 10);

    // Thin passthrough to IDiskUsageRepository::childrenOf, for redisplaying a folder's cached
    // children without rescanning (e.g. after a segment-click navigation).
    Result<std::vector<DiskUsageNode>> childrenFor(const std::filesystem::path& folder) const;

private:
    IDiskUsageScanner& m_scanner;
    IDiskUsageRepository& m_repository;
};
