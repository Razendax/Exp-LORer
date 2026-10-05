#pragma once

#include "IDuplicateFileScanner.h"

class IFileSystemRepository;

// Implements IDuplicateFileScanner (Architecture.md §14.51): recursive enumeration via the
// existing IFileSystemRepository::listDirectoryRecursive, bucketed first by exact size then by
// full-content xxHash64 (StandardFileHasher), then byte-for-byte verified before being confirmed
// as a duplicate group -- a rare hash collision must never make two genuinely different files
// silently vanish from the results. Portable (no Windows-only API), unlike WindowsDiskUsageScanner.
class StandardDuplicateFileScanner : public IDuplicateFileScanner
{
public:
    explicit StandardDuplicateFileScanner(IFileSystemRepository& fileSystemRepository);

    Result<std::vector<DuplicateFileGroup>> scan(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress,
        const std::atomic<bool>& cancelRequested) const override;

private:
    IFileSystemRepository& m_fileSystemRepository;
};
