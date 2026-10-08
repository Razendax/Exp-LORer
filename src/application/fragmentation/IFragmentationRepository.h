#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "FileFragmentationInfo.h"
#include "Result.h"

// Port for persisting/reading back fragmentation scan results (Architecture.md §14.52). Implemented
// by src/adapters/fragmentation/SQLiteFragmentationRepository. Mirrors IDiskUsageRepository --
// unlike IDuplicateFileScanner's output, a stale cached fragment count is harmless to redisplay
// (this feature drives no delete action), so persisting is safe.
class IFragmentationRepository
{
public:
    virtual ~IFragmentationRepository() = default;

    // Batched upsert of one scan's files in one transaction. Rows previously recorded anywhere
    // under scanRoot whose size is >= minFileSizeBytes but absent from `files` this time are
    // deleted (removed from disk since the last scan at that size threshold); rows below
    // minFileSizeBytes are left untouched, since this scan never examined them (a different,
    // smaller minFileSizeBytes on a later scan of the same root must not look like every bigger
    // file vanished).
    virtual Result<void> saveScan(const std::filesystem::path& scanRoot, std::uintmax_t minFileSizeBytes,
                                   const std::vector<FileFragmentationInfo>& files) = 0;

    // Every persisted row strictly under `folder`, from the most recent scan(s) that covered
    // them. Empty (not an error) if nothing under `folder` has ever been scanned.
    virtual Result<std::vector<FileFragmentationInfo>> filesUnder(const std::filesystem::path& folder) const = 0;
};
