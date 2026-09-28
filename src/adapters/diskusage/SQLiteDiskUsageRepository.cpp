#include "SQLiteDiskUsageRepository.h"

#include <algorithm>
#include <filesystem>

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

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
        return Error(fallback, sqlError.text().toStdString());
    }

    // '<' can never appear anywhere in a real Windows path (fully reserved, unlike '%'/'_' which
    // are valid filename characters), so it's safe as this LIKE pattern's escape character without
    // ambiguity -- see prefixPattern()'s use below. Avoids substr()-with-a-computed-length
    // entirely, which would need to count in whatever unit SQLite's `substr` uses internally and
    // could silently misalign for names containing characters outside the BMP (emoji).
    QString escapeLikeWildcards(const QString& text)
    {
        QString escaped = text;
        escaped.replace(QLatin1Char('%'), QStringLiteral("<%"));
        escaped.replace(QLatin1Char('_'), QStringLiteral("<_"));
        return escaped;
    }

    // scanRoot's path text with a trailing path separator, for LIKE-prefix matching its
    // descendants (never scanRoot's own row -- callers match that separately by exact path).
    QString descendantPrefixPattern(const fs::path& scanRoot)
    {
        std::wstring native = scanRoot.wstring();
        if (native.empty() || native.back() != fs::path::preferred_separator)
        {
            native.push_back(fs::path::preferred_separator);
        }
        return escapeLikeWildcards(QString::fromStdWString(native)) + QStringLiteral("%");
    }
}

SQLiteDiskUsageRepository::SQLiteDiskUsageRepository(const std::filesystem::path& databasePath)
    : m_connectionName(QStringLiteral("SQLiteDiskUsageRepository_%1").arg(reinterpret_cast<quintptr>(this)))
{
    if (databasePath != fs::path(":memory:") && !databasePath.parent_path().empty())
    {
        fs::create_directories(databasePath.parent_path());
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(toQString(databasePath));
    m_db.open();

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    // This repository and SQLiteTagRepository open independent connections to the same file from
    // different threads (Architecture.md §14.39) -- a busy timeout keeps a lock-contended writer
    // waiting briefly instead of failing immediately with SQLITE_BUSY, same rationale as
    // SQLiteTagRepository's own busy_timeout pragma.
    QSqlQuery busyTimeout(m_db);
    busyTimeout.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

    runMigrations();
}

SQLiteDiskUsageRepository::~SQLiteDiskUsageRepository()
{
    m_db.close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void SQLiteDiskUsageRepository::runMigrations()
{
    QSqlQuery migration(m_db);
    migration.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS filesystem_index (
            folder_id     INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id     INTEGER REFERENCES filesystem_index(folder_id) ON DELETE CASCADE,
            path          TEXT NOT NULL UNIQUE,
            name          TEXT NOT NULL,
            size          INTEGER NOT NULL,
            is_directory  INTEGER NOT NULL,
            scanned_at    INTEGER NOT NULL
        )
    )"));
    migration.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_filesystem_index_parent ON filesystem_index(parent_id)"));
}

Result<void> SQLiteDiskUsageRepository::saveScan(const std::filesystem::path& scanRoot, const std::vector<DiskUsageNode>& nodes)
{
    QSqlQuery begin(m_db);
    // IMMEDIATE (not the default DEFERRED) acquires the write lock up front rather than only at
    // the first actual write -- with a concurrent reader connection to the same file (e.g.
    // AdvancedSearchWorker's own SQLiteTagRepository connection, Architecture.md §14.39), failing
    // fast here is preferable to discovering a lock conflict partway through pass 1's upserts.
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE")))
    {
        return Result<void>::failure(toError(begin.lastError()));
    }

    // Strictly greater than every scanned_at value written so far -- not just
    // currentMSecsSinceEpoch(), since two scans issued in quick succession (a user re-clicking
    // "Scan Folder", or two calls against an in-memory DB with no real I/O latency between them)
    // could otherwise land in the same millisecond and share one scanned_at value, silently
    // disabling the staleness comparison below (`scanned_at < sessionTimestamp` would never fire,
    // leaving rows removed from disk since the *previous* scan undeleted).
    QSqlQuery maxTimestamp(m_db);
    maxTimestamp.exec(QStringLiteral("SELECT MAX(scanned_at) FROM filesystem_index"));
    const qint64 previousMax = maxTimestamp.next() ? maxTimestamp.value(0).toLongLong() : 0;
    const qint64 sessionTimestamp = std::max(QDateTime::currentMSecsSinceEpoch(), previousMax + 1);

    // Pass 1: upsert every node's own row by path.
    for (const DiskUsageNode& node : nodes)
    {
        QSqlQuery upsert(m_db);
        upsert.prepare(QStringLiteral(R"(
            INSERT INTO filesystem_index(path, name, size, is_directory, scanned_at)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(path) DO UPDATE SET
                name = excluded.name,
                size = excluded.size,
                is_directory = excluded.is_directory,
                scanned_at = excluded.scanned_at
        )"));
        upsert.addBindValue(toQString(node.path));
        upsert.addBindValue(QString::fromStdString(node.name));
        upsert.addBindValue(static_cast<qint64>(node.size));
        upsert.addBindValue(node.isDirectory ? 1 : 0);
        upsert.addBindValue(sessionTimestamp);
        if (!upsert.exec())
        {
            Error error = toError(upsert.lastError());
            m_db.rollback();
            return Result<void>::failure(std::move(error));
        }
    }

    // Pass 2: resolve each row's parent_id now that every node in this scan has a folder_id.
    for (const DiskUsageNode& node : nodes)
    {
        const fs::path parentPath = node.path.parent_path();
        if (parentPath.empty() || parentPath == node.path)
        {
            continue; // scan root with no scanned parent -- leave parent_id NULL.
        }

        QSqlQuery updateParent(m_db);
        updateParent.prepare(QStringLiteral(R"(
            UPDATE filesystem_index
            SET parent_id = (SELECT folder_id FROM filesystem_index WHERE path = ?)
            WHERE path = ?
        )"));
        updateParent.addBindValue(toQString(parentPath));
        updateParent.addBindValue(toQString(node.path));
        if (!updateParent.exec())
        {
            Error error = toError(updateParent.lastError());
            m_db.rollback();
            return Result<void>::failure(std::move(error));
        }
    }

    // Delete stale rows: previously recorded under scanRoot (or scanRoot's own prior row) but not
    // touched by this scan -- removed from disk since the last time this subtree was scanned.
    QSqlQuery deleteStale(m_db);
    deleteStale.prepare(QStringLiteral(R"(
        DELETE FROM filesystem_index
        WHERE scanned_at < ?
        AND (path = ? OR path LIKE ? ESCAPE '<')
    )"));
    deleteStale.addBindValue(sessionTimestamp);
    deleteStale.addBindValue(toQString(scanRoot));
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

    return Result<void>::success();
}

Result<std::vector<DiskUsageNode>> SQLiteDiskUsageRepository::childrenOf(const std::filesystem::path& folder) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"(
        SELECT path, name, size, is_directory
        FROM filesystem_index
        WHERE parent_id = (SELECT folder_id FROM filesystem_index WHERE path = ?)
    )"));
    query.addBindValue(toQString(folder));

    if (!query.exec())
    {
        return Result<std::vector<DiskUsageNode>>::failure(toError(query.lastError()));
    }

    std::vector<DiskUsageNode> nodes;
    while (query.next())
    {
        DiskUsageNode node;
        node.path = toPath(query.value(0).toString());
        node.name = query.value(1).toString().toStdString();
        node.size = static_cast<std::uintmax_t>(query.value(2).toLongLong());
        node.isDirectory = query.value(3).toInt() != 0;
        nodes.push_back(std::move(node));
    }

    return Result<std::vector<DiskUsageNode>>::success(std::move(nodes));
}
