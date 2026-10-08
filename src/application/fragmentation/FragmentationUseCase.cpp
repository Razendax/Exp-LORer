#include "FragmentationUseCase.h"

#include <algorithm>

FragmentationUseCase::FragmentationUseCase(IFragmentationScanner& scanner, IFragmentationRepository& repository)
    : m_scanner(scanner)
    , m_repository(repository)
{
}

Result<std::vector<FileFragmentationInfo>> FragmentationUseCase::scanAndPersist(
    const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    const std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)>&
        onProgress,
    const std::atomic<bool>& cancelRequested)
{
    auto scanned = m_scanner.scan(root, minFileSizeBytes, onProgress, cancelRequested);
    if (scanned.hasError())
    {
        return scanned;
    }

    auto saved = m_repository.saveScan(root, minFileSizeBytes, scanned.value());
    if (saved.hasError())
    {
        return Result<std::vector<FileFragmentationInfo>>::failure(std::move(saved).error());
    }

    return scanned;
}

Result<std::vector<FileFragmentationInfo>> FragmentationUseCase::filesUnder(const std::filesystem::path& folder) const
{
    return m_repository.filesUnder(folder);
}

FragmentationSummary FragmentationUseCase::summarize(const std::vector<FileFragmentationInfo>& files)
{
    FragmentationSummary summary;
    summary.filesScanned = files.size();
    for (const FileFragmentationInfo& file : files)
    {
        summary.totalBytesScanned += file.sizeBytes;
        summary.totalFragments += file.fragmentCount;
        if (file.fragmentCount > 1)
        {
            ++summary.fragmentedFileCount;
        }
    }
    return summary;
}

std::vector<FileFragmentationInfo> FragmentationUseCase::sortedByFragmentCount(std::vector<FileFragmentationInfo> files)
{
    std::sort(files.begin(), files.end(), [](const FileFragmentationInfo& lhs, const FileFragmentationInfo& rhs) {
        return lhs.fragmentCount > rhs.fragmentCount;
    });
    return files;
}
