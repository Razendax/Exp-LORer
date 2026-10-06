#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>

#include "DiskUsageNode.h"
#include "Result.h"

class IDiskUsageScanner;
class DiskUsageUseCase;
class SQLiteDiskUsageRepository;

// Runs the I/O parts of disk-usage scanning/lookup (Architecture.md §14.39) on whatever thread it
// has been moved to. Owned and driven by DiskUsageService, same shape as AdvancedSearchWorker.
//
// Like AdvancedSearchWorker, this worker cannot borrow CompositionRoot's shared connections --
// SQLiteDiskUsageRepository wraps a thread-affine QSqlDatabase connection -- so it owns its own,
// opened lazily on this worker's own thread the first time it's actually needed (not in the
// constructor, which still runs on whatever thread constructs DiskUsageService, before
// moveToThread takes effect).
class DiskUsageWorker : public QObject
{
    Q_OBJECT

public:
    DiskUsageWorker(IDiskUsageScanner& scanner, const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~DiskUsageWorker() override;

    // Scans root and persists the result. Returns success/failure only -- the (potentially huge,
    // e.g. a whole-drive scan) node list itself is never marshaled back to the UI thread; a caller
    // that wants to display something after a successful scan issues a separate, cheap
    // childrenFor() request for just the folder it wants to show.
    Result<void> scanAndPersist(const std::filesystem::path& root, const std::shared_ptr<std::atomic<bool>>& cancelFlag,
                                 const std::function<void(std::size_t, const std::filesystem::path&)>& onProgress);

    // Cached immediate children of `folder` from the most recent scan that covered it.
    Result<std::vector<DiskUsageNode>> childrenFor(const std::filesystem::path& folder);

private:
    void ensureUseCase();

    IDiskUsageScanner& m_scanner;
    std::filesystem::path m_databasePath;

    // This worker's own connection, constructed lazily on this thread (see class comment).
    std::unique_ptr<SQLiteDiskUsageRepository> m_repository;
    std::unique_ptr<DiskUsageUseCase> m_useCase;
};
