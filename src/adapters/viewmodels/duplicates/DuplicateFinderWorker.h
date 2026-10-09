#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>

#include "DuplicateFileGroup.h"
#include "DuplicateFinderUseCase.h"
#include "Result.h"

class IFileSystemRepository;

// One file within a confirmed duplicate group, paired with its modification time for the dialog's
// "modified date" column and its "keep newest/oldest" helper buttons -- DuplicateFileGroup itself
// (Application layer) deliberately carries no per-file metadata beyond the path, so this pairing
// lives here instead.
struct DuplicateFileInfo
{
    std::filesystem::path path;
    std::chrono::system_clock::time_point modifiedAt;
};

// A scanned duplicate group plus per-file display metadata, parallel to DuplicateFileGroup::paths.
struct DuplicateGroupInfo
{
    DuplicateFileGroup group;
    std::vector<DuplicateFileInfo> files;
};

// Runs the I/O parts of a duplicate-file scan (Architecture.md §14.51) on whatever thread it has
// been moved to. Owned and driven by DuplicateFinderService, same shape as FileTransferWorker.
// Besides the scan itself, stats every confirmed-group path (still on this background thread, never
// the Qt UI thread) to attach the modification time the dialog's UI needs.
class DuplicateFinderWorker : public QObject
{
    Q_OBJECT

public:
    DuplicateFinderWorker(DuplicateFinderUseCase& useCase, IFileSystemRepository& fileSystemRepository,
                           QObject* parent = nullptr);

    Result<std::vector<DuplicateGroupInfo>> runScan(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::shared_ptr<std::atomic<bool>>& cancelFlag,
        const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress);

private:
    DuplicateFinderUseCase& m_useCase;
    IFileSystemRepository& m_fileSystemRepository;
};
