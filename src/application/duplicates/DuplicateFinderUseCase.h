#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "DuplicateFileGroup.h"
#include "IDuplicateFileScanner.h"
#include "Result.h"

// Orchestrates duplicate-file scanning (Architecture.md §14.51). Stateless, depends only on
// IDuplicateFileScanner -- same "never call another use case" posture as FilePreviewUseCase/
// FileTransferUseCase. A single shared instance may serve concurrent callers.
class DuplicateFinderUseCase
{
public:
    explicit DuplicateFinderUseCase(IDuplicateFileScanner& scanner);

    Result<std::vector<DuplicateFileGroup>> scan(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress,
        const std::atomic<bool>& cancelRequested) const;

    // Pure, no I/O: sum of (count(group) - 1) * group.fileSize across every group -- the bytes that
    // would be freed by keeping exactly one copy of each.
    static std::uintmax_t totalWastedBytes(const std::vector<DuplicateFileGroup>& groups);

    // Pure, no I/O: descending sort by each group's own wasted bytes ((count - 1) * fileSize).
    static std::vector<DuplicateFileGroup> groupsSortedByWaste(std::vector<DuplicateFileGroup> groups);

private:
    IDuplicateFileScanner& m_scanner;
};
