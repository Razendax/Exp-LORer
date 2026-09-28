#pragma once

#include <filesystem>

#include <QSqlDatabase>
#include <QString>

#include "IDiskUsageRepository.h"

// Implements IDiskUsageRepository using QSqlDatabase/QSQLITE against the `filesystem_index` table
// (Architecture.md §7, §14.39), mirroring SQLiteTagRepository's connection-per-instance posture but
// deliberately NOT sharing its PRAGMA user_version counter -- that counter belongs to the
// Tags/Files/FileTags schema owned by a different repository/connection; filesystem_index's
// CREATE TABLE IF NOT EXISTS is idempotent and sufficient for one table with no evolving schema yet
// (documented exception, Architecture.md §7).
class SQLiteDiskUsageRepository : public IDiskUsageRepository
{
public:
    // `databasePath` may be ":memory:" for tests (Architecture.md §11). Its parent directory is
    // created if missing.
    explicit SQLiteDiskUsageRepository(const std::filesystem::path& databasePath);
    ~SQLiteDiskUsageRepository() override;

    SQLiteDiskUsageRepository(const SQLiteDiskUsageRepository&) = delete;
    SQLiteDiskUsageRepository& operator=(const SQLiteDiskUsageRepository&) = delete;

    Result<void> saveScan(const std::filesystem::path& scanRoot, const std::vector<DiskUsageNode>& nodes) override;
    Result<std::vector<DiskUsageNode>> childrenOf(const std::filesystem::path& folder) const override;

private:
    void runMigrations();

    QSqlDatabase m_db;
    QString m_connectionName;
};
