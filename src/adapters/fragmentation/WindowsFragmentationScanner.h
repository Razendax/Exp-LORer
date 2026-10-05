#pragma once

#include "IFragmentationScanner.h"

class IFileSystemRepository;

// Implements IFragmentationScanner (Architecture.md §14.52): recursive enumeration via the existing
// IFileSystemRepository::listDirectoryRecursive, then per candidate file, counts physical NTFS
// extents via FSCTL_GET_RETRIEVAL_POINTERS -- the same primitive Windows' own
// defrag.exe/"Optimize Drives" use internally. Windows-only; scan() fails with an IoError
// "Not supported on this platform" everywhere else, same posture as WindowsDiskUsageScanner.
class WindowsFragmentationScanner : public IFragmentationScanner
{
public:
    explicit WindowsFragmentationScanner(IFileSystemRepository& fileSystemRepository);

    Result<std::vector<FileFragmentationInfo>> scan(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)>&
            onProgress,
        const std::atomic<bool>& cancelRequested) const override;

private:
    IFileSystemRepository& m_fileSystemRepository;
};
