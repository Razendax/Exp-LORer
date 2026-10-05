#include "SQLiteFragmentationRepository.h"

#include <algorithm>
#include <filesystem>

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "Logging.h"

namespace
{
    namespace fs = std::filesystem;

    QString toQString(const fs::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    fs::path toPath(const QString& text)
    {
        return fs::path(text.toStdWString());
    }

    Error toError(const QSqlError& sqlError, ErrorCode fallback = ErrorCode::IoError)
    {
        // Never log sqlError.text() -- it can echo bound values (Architecture.md §14.23).
        Logging::warn("SQLiteFragmentationRepository query failed: " + std::string(toLogString(fallback)));
        return Error(fallback, sqlError.text().toStdString());
    }

    // '<' can never appear in a real Windows path, so it's safe as this LIKE pattern's escape
    // character -- same idiom SQLiteDiskUsageRepository::saveScan already established and proved.
    QString escapeLikeWildcards(const QString& text)
    {
        QString escaped = text;
        escaped.replace(QLatin1Char('%'), QStringLiteral("<%"));
        escaped.replace(QLatin1Char('_'), QStringLiteral("<_"));
        return escaped;
    }

    // folder's path text with a trailing path separator, for LIKE-prefix matching rows strictly
    // under it (this table never stores a row for the folder/drive root itself -- files only).
    QString descendantPrefixPattern(const fs::path& folder)
    {
        std::wstring native = folder.wstring();
        if (native.empty() || native.back() != fs::path::preferred_separator)
        {
            native.push_back(fs::path::preferred_separator);
        }
        return escapeLikeWildcards(QString::fromStdWString(native)) + QStringLiteral("%");
    }
}

SQLiteFragmentationRepository::SQLiteFragmentationRepository(const std::filesystem::path& databasePath)
    : m_connectionName(QStringLiteral("SQLiteFragmentationRepository_%1").arg(reinterpret_cast<quintptr>(this)))
{
    if (databasePath != fs::path(":memory:") && !databasePath.parent_path().empty())
    {
        fs::create_directories(databasePath.parent_path());
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(toQString(databasePath));
    m_db.open();

    // A second, independent connection to the same explorer.db file as SQLiteTagRepository's and
    // SQLiteDiskUsageRepository's own connections -- a busy timeout keeps a lock-contended writer
    // waiting briefly instead of failing immediately with SQLITE_BUSY, same rationale as those.
    QSqlQuery busyTimeout(m_db);
    busyTimeout.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

    runMigrations();
}

SQLiteFragmentationRepository::~SQLiteFragmentationRepository()
{
    m_db.close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void SQLiteFragmentationRepository::runMigrations()
{
    QSqlQuery migration(m_db);
    migration.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS file_fragmentation (
            path            TEXT PRIMARY KEY,
            size            INTEGER NOT NULL,
            fragment_count  INTEGER NOT NULL,
            scanned_at      INTEGER NOT NULL
        )
    )"));

    Logging::info("SQLiteFragmentationRepository: migration applied");
}

Result<void> SQLiteFragmentationRepository::saveScan(const std::filesystem::path& scanRoot, std::uintmax_t minFileSizeBytes,
                                                       const std::vector<FileFragmentationInfo>& files)
{
    QSqlQuery begin(m_db);
    // IMMEDIATE acquires the write lock up front rather than only at the first actual write, same
    // "fail fast against a concurrent reader connection" rationale as SQLiteDiskUsageRepository.
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE")))
    {
        return Result<void>::failure(toError(begin.lastError()));
    }

    // Strictly greater than every scanned_at value written so far, same race-safety trick
    // SQLiteDiskUsageRepository::saveScan already uses -- two scans issued close together must not
    // share one scanned_at value.
    QSqlQuery maxTimestamp(m_db);
    maxTimestamp.exec(QStringLiteral("SELECT MAX(scanned_at) FROM file_fragmentation"));
    const qint64 previousMax = maxTimestamp.next() ? maxTimestamp.value(0).toLongLong() : 0;
    const qint64 sessionTimestamp = std::max(QDateTime::currentMSecsSinceEpoch(), previousMax + 1);

    for (const FileFragmentationInfo& file : files)
    {
        QSqlQuery upsert(m_db);
        upsert.prepare(QStringLiteral(R"(
            INSERT INTO file_fragmentation(path, size, fragment_count, scanned_at)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(path) DO UPDATE SET
                size = excluded.size,
                fragment_count = excluded.fragment_count,
                scanned_at = excluded.scanned_at
        )"));
        upsert.addBindValue(toQString(file.path));
        upsert.addBindValue(static_cast<qint64>(file.sizeBytes));
        upsert.addBindValue(static_cast<qint64>(file.fragmentCount));
        upsert.addBindValue(sessionTimestamp);
        if (!upsert.exec())
        {
            Error error = toError(upsert.lastError());
            m_db.rollback();
            return Result<void>::failure(std::move(error));
        }
    }

    // Delete stale rows: previously recorded strictly under scanRoot, at or above this scan's size
    // threshold, but not touched by this scan -- removed from disk since the last scan at that
    // threshold. Rows below minFileSizeBytes are left untouched -- this scan never examined them.
    QSqlQuery deleteStale(m_db);
    deleteStale.prepare(QStringLiteral(R"(
        DELETE FROM file_fragmentation
        WHERE scanned_at < ? AND size >= ? AND path LIKE ? ESCAPE '<'
    )"));
    deleteStale.addBindValue(sessionTimestamp);
    deleteStale.addBindValue(static_cast<qint64>(minFileSizeBytes));
    deleteStale.addBindValue(descendantPrefixPattern(scanRoot));
    if (!deleteStale.exec())
    {
        Error error = toError(deleteStale.lastError());
        m_db.rollback();
        return Result<void>::failure(std::move(error));
    }

    if (!m_db.commit())
    {
        Error error = toError(m_db.lastError());
        m_db.rollback();
        return Result<void>::failure(std::move(error));
    }

    Logging::info("SQLiteFragmentationRepository: scan saved (" + std::to_string(files.size()) + " file(s))");
    return Result<void>::success();
}

Result<std::vector<FileFragmentationInfo>> SQLiteFragmentationRepository::filesUnder(const std::filesystem::path& folder) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"(
        SELECT path, size, fragment_count
        FROM file_fragmentation
        WHERE path LIKE ? ESCAPE '<'
    )"));
    query.addBindValue(descendantPrefixPattern(folder));

    if (!query.exec())
    {
        return Result<std::vector<FileFragmentationInfo>>::failure(toError(query.lastError()));
    }

    std::vector<FileFragmentationInfo> files;
    while (query.next())
    {
        FileFragmentationInfo file;
        file.path = toPath(query.value(0).toString());
        file.sizeBytes = static_cast<std::uintmax_t>(query.value(1).toLongLong());
        file.fragmentCount = static_cast<std::uint32_t>(query.value(2).toLongLong());
        files.push_back(std::move(file));
    }

    Logging::debug("SQLiteFragmentationRepository: filesUnder returned " + std::to_string(files.size()) + " file(s)");
    return Result<std::vector<FileFragmentationInfo>>::success(std::move(files));
}
