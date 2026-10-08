#include "DuplicateFinderUseCase.h"

#include <algorithm>

DuplicateFinderUseCase::DuplicateFinderUseCase(IDuplicateFileScanner& scanner)
    : m_scanner(scanner)
{
}

Result<std::vector<DuplicateFileGroup>> DuplicateFinderUseCase::scan(
    const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress,
    const std::atomic<bool>& cancelRequested) const
{
    return m_scanner.scan(root, minFileSizeBytes, onProgress, cancelRequested);
}

namespace
{
    std::uintmax_t wastedBytes(const DuplicateFileGroup& group)
    {
        if (group.paths.size() <= 1)
        {
            return 0;
        }
        return (group.paths.size() - 1) * group.fileSize;
    }
}

std::uintmax_t DuplicateFinderUseCase::totalWastedBytes(const std::vector<DuplicateFileGroup>& groups)
{
    std::uintmax_t total = 0;
    for (const auto& group : groups)
    {
        total += wastedBytes(group);
    }
    return total;
}

std::vector<DuplicateFileGroup> DuplicateFinderUseCase::groupsSortedByWaste(std::vector<DuplicateFileGroup> groups)
{
    std::sort(groups.begin(), groups.end(),
              [](const DuplicateFileGroup& lhs, const DuplicateFileGroup& rhs) { return wastedBytes(lhs) > wastedBytes(rhs); });
    return groups;
}
