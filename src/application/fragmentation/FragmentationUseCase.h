#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "FileFragmentationInfo.h"
#include "FragmentationSummary.h"
#include "IFragmentationRepository.h"
#include "IFragmentationScanner.h"
#include "Result.h"

// Orchestrates fragmentation scanning + persistence (Architecture.md §14.52). Stateless, same
// "orchestrates scanning + persistence" shape as DiskUsageUseCase (not DuplicateFinderUseCase's
// scanner-only shape) -- a single shared instance may serve concurrent callers.
class FragmentationUseCase
{
public:
    FragmentationUseCase(IFragmentationScanner& scanner, IFragmentationRepository& repository);

    // Scans root and persists the result only on success -- a cancelled or failed scan is never
    // persisted, so an interrupted scan can't delete stale rows for files it never got to
    // re-examine (same reasoning IFragmentationRepository::saveScan's own comment states for the
    // size-threshold case). Returns the full scanned list on success.
    Result<std::vector<FileFragmentationInfo>> scanAndPersist(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)>&
            onProgress,
        const std::atomic<bool>& cancelRequested);

    // Thin passthrough to IFragmentationRepository::filesUnder, for cache-only redisplay (no scan).
    Result<std::vector<FileFragmentationInfo>> filesUnder(const std::filesystem::path& folder) const;

    // Pure, no I/O.
    static FragmentationSummary summarize(const std::vector<FileFragmentationInfo>& files);

    // Pure, no I/O: descending sort by fragmentCount, so the most-fragmented files surface first.
    static std::vector<FileFragmentationInfo> sortedByFragmentCount(std::vector<FileFragmentationInfo> files);

private:
    IFragmentationScanner& m_scanner;
    IFragmentationRepository& m_repository;
};
