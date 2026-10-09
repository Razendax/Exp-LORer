#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>

#include "FileFragmentationInfo.h"
#include "Result.h"

class IFragmentationScanner;
class FragmentationUseCase;
class SQLiteFragmentationRepository;

// Runs the I/O parts of fragmentation scanning/lookup (Architecture.md §14.52) on whatever thread
// it has been moved to. Owned and driven by FragmentationService, same shape as DiskUsageWorker.
//
// Like DiskUsageWorker, this worker cannot borrow CompositionRoot's shared connections --
// SQLiteFragmentationRepository wraps a thread-affine QSqlDatabase connection -- so it owns its
// own, opened lazily on this worker's own thread the first time it's actually needed (not in the
// constructor, which still runs on whatever thread constructs FragmentationService).
class FragmentationWorker : public QObject
{
    Q_OBJECT

public:
    FragmentationWorker(IFragmentationScanner& scanner, const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~FragmentationWorker() override;

    // Scans root and persists the result on success. Returns success/failure only -- the
    // (potentially huge, e.g. a whole-drive scan) file list itself is never marshaled back to the
    // UI thread; a caller that wants to display something after a successful scan issues a
    // separate, cheap filesUnder() request for just the folder it wants to show.
    Result<void> scanAndPersist(
        const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
        const std::shared_ptr<std::atomic<bool>>& cancelFlag,
        const std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)>&
            onProgress);

    // Cached files strictly under `folder` from the most recent scan(s) that covered them.
    Result<std::vector<FileFragmentationInfo>> filesUnder(const std::filesystem::path& folder);

private:
    void ensureUseCase();

    IFragmentationScanner& m_scanner;
    std::filesystem::path m_databasePath;

    // This worker's own connection, constructed lazily on this thread (see class comment).
    std::unique_ptr<SQLiteFragmentationRepository> m_repository;
    std::unique_ptr<FragmentationUseCase> m_useCase;
};
