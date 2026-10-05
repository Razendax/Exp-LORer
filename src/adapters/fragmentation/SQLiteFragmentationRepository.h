#pragma once

#include <filesystem>

#include <QSqlDatabase>
#include <QString>

#include "IFragmentationRepository.h"

// Implements IFragmentationRepository using QSqlDatabase/QSQLITE against the `file_fragmentation`
// table (Architecture.md §7, §14.52), mirroring SQLiteDiskUsageRepository's connection-per-instance
// posture -- its own CREATE TABLE IF NOT EXISTS, not gated by the PRAGMA user_version counter that
// owns the Tags/Files/FileTags schema.
class SQLiteFragmentationRepository : public IFragmentationRepository
{
public:
    // `databasePath` may be ":memory:" for tests (Architecture.md §11). Its parent directory is
    // created if missing.
    explicit SQLiteFragmentationRepository(const std::filesystem::path& databasePath);
    ~SQLiteFragmentationRepository() override;

    SQLiteFragmentationRepository(const SQLiteFragmentationRepository&) = delete;
    SQLiteFragmentationRepository& operator=(const SQLiteFragmentationRepository&) = delete;

    Result<void> saveScan(const std::filesystem::path& scanRoot, std::uintmax_t minFileSizeBytes,
                           const std::vector<FileFragmentationInfo>& files) override;
    Result<std::vector<FileFragmentationInfo>> filesUnder(const std::filesystem::path& folder) const override;

private:
    void runMigrations();

    QSqlDatabase m_db;
    QString m_connectionName;
};
