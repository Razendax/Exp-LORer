#include "DiskUsageUseCase.h"

#include <algorithm>

DiskUsageUseCase::DiskUsageUseCase(IDiskUsageScanner& scanner, IDiskUsageRepository& repository)
    : m_scanner(scanner)
    , m_repository(repository)
{
}

Result<std::vector<DiskUsageNode>> DiskUsageUseCase::scanAndPersist(
    const std::filesystem::path& root,
    const std::function<void(std::size_t scanned, const std::filesystem::path& currentPath)>& onProgress,
    const std::atomic<bool>& cancelRequested)
{
    auto scanned = m_scanner.scan(root, onProgress, cancelRequested);
    if (scanned.hasError())
    {
        return scanned;
    }

    auto saved = m_repository.saveScan(root, scanned.value());
    if (saved.hasError())
    {
        return Result<std::vector<DiskUsageNode>>::failure(std::move(saved).error());
    }

    return scanned;
}

std::vector<DiskUsageNode> DiskUsageUseCase::topChildren(std::vector<DiskUsageNode> children, std::size_t max)
{
    std::sort(children.begin(), children.end(),
              [](const DiskUsageNode& lhs, const DiskUsageNode& rhs) { return lhs.size > rhs.size; });

    if (children.size() > max)
    {
        children.resize(max);
    }

    return children;
}

Result<std::vector<DiskUsageNode>> DiskUsageUseCase::childrenFor(const std::filesystem::path& folder) const
{
    return m_repository.childrenOf(folder);
}
