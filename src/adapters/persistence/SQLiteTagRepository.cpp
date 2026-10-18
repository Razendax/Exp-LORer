#include "SQLiteTagRepository.h"

#include <algorithm>
#include <filesystem>

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "Logging.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    std::filesystem::path toPath(const QString& text)
    {
        return std::filesystem::path(text.toStdWString());
    }

    QString hashToText(std::uint64_t hash)
    {
        return QString::number(hash);
    }

    // SQLite's compiled SQLITE_MAX_VARIABLE_NUMBER defaults to 999 on many builds -- tagsForFiles
    // binds one placeholder per path, so a single query over a directory with more files than this
    // would fail outright with "too many SQL variables" and silently blank the Tags column for the
    // whole directory. Chunking keeps every query comfortably under that limit regardless of the
    // actual compiled-in value.
    constexpr int kTagsForFilesBatchSize = 500;

    Error toError(const QSqlError& sqlError, ErrorCode fallback = ErrorCode::IoError)
    {
        // Never log sqlError.text() -- it can echo bound values (Architecture.md §14.23).
        Logging::warn("SQLiteTagRepository query failed: " + std::string(toLogString(fallback)));
        return Error(fallback, sqlError.text().toStdString());
    }

    bool isUniqueConstraintViolation(const QSqlError& sqlError)
    {
        return sqlError.text().contains(QStringLiteral("UNIQUE constraint failed"), Qt::CaseInsensitive);
    }
}

SQLiteTagRepository::SQLiteTagRepository(const std::filesystem::path& databasePath)
    : m_connectionName(QStringLiteral("SQLiteTagRepository_%1").arg(reinterpret_cast<quintptr>(this)))
{
    if (databasePath != std::filesystem::path(":memory:") && !databasePath.parent_path().empty())
    {
        std::filesystem::create_directories(databasePath.parent_path());
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(toQString(databasePath));
    m_db.open();

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    // AdvancedSearchWorker (Architecture.md §14.x) opens its own connection to this same file on
    // a background thread, so a write here (tagging a file) can now race a concurrent read there.
    // Without a busy timeout, the loser fails immediately with SQLITE_BUSY instead of waiting
    // briefly for the lock to clear.
    QSqlQuery busyTimeout(m_db);
    busyTimeout.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

    runMigrations();
}

SQLiteTagRepository::~SQLiteTagRepository()
{
    m_db.close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void SQLiteTagRepository::runMigrations()
{
    QSqlQuery versionQuery(m_db);
    versionQuery.exec(QStringLiteral("PRAGMA user_version"));
    int version = 0;
    if (versionQuery.next())
    {
        version = versionQuery.value(0).toInt();
    }

    if (version >= 1)
    {
        return;
    }

    QSqlQuery migration(m_db);
    const bool migrated = migration.exec(QStringLiteral(R"(
                                    CREATE TABLE IF NOT EXISTS Tags (
                                        id          INTEGER PRIMARY KEY AUTOINCREMENT,
                                        name        TEXT NOT NULL UNIQUE,
                                        hex_color   TEXT NOT NULL,
                                        created_at  INTEGER NOT NULL
                                    )
                                )"))
                           && migration.exec(QStringLiteral(R"(
                                    CREATE TABLE IF NOT EXISTS Files (
                                        id            INTEGER PRIMARY KEY AUTOINCREMENT,
                                        path          TEXT NOT NULL UNIQUE,
                                        file_hash     TEXT NOT NULL,
                                        size          INTEGER NOT NULL DEFAULT 0,
                                        modified_at   INTEGER NOT NULL DEFAULT 0,
                                        last_seen_at  INTEGER NOT NULL
                                    )
                                )"))
                           && migration.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_files_hash ON Files(file_hash)"))
                           && migration.exec(QStringLiteral(R"(
                                    CREATE TABLE IF NOT EXISTS FileTags (
                                        file_id  INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,
                                        tag_id   INTEGER NOT NULL REFERENCES Tags(id) ON DELETE CASCADE,
                                        PRIMARY KEY (file_id, tag_id)
                                    )
                                )"));

    if (!migrated)
    {
        // Leaving user_version at 0 means a retry on the next launch will attempt these
        // CREATE-TABLE-IF-NOT-EXISTS statements again rather than silently trusting a DB that may
        // be missing tables (disk full, read-only file, etc.).
        Logging::warn("SQLiteTagRepository: migration to version 1 failed: " + migration.lastError().text().toStdString());
        return;
    }

    if (!migration.exec(QStringLiteral("PRAGMA user_version = 1")))
    {
        Logging::warn("SQLiteTagRepository: failed to record migration version 1: "
                       + migration.lastError().text().toStdString());
        return;
    }

    Logging::info("SQLiteTagRepository: migration applied (version 1)");
}

Result<Tag> SQLiteTagRepository::createTag(const std::string& name, const std::string& hexColor)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO Tags(name, hex_color, created_at) VALUES(?, ?, ?)"));
    query.addBindValue(QString::fromStdString(name));
    query.addBindValue(QString::fromStdString(hexColor));
    query.addBindValue(static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));

    if (!query.exec())
    {
        if (isUniqueConstraintViolation(query.lastError()))
        {
            return Result<Tag>::failure(Error(ErrorCode::AlreadyExists, "A tag with this name already exists"));
        }
        return Result<Tag>::failure(toError(query.lastError()));
    }

    const Tag::Id id = query.lastInsertId().toLongLong();
    Logging::debug("SQLiteTagRepository: tag created");
    return Tag::create(id, name, hexColor);
}

Result<void> SQLiteTagRepository::updateTag(const Tag& tag)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE Tags SET name = ?, hex_color = ? WHERE id = ?"));
    query.addBindValue(QString::fromStdString(tag.name()));
    query.addBindValue(QString::fromStdString(tag.hexColor()));
    query.addBindValue(static_cast<qint64>(tag.id()));

    if (!query.exec())
    {
        if (isUniqueConstraintViolation(query.lastError()))
        {
            return Result<void>::failure(Error(ErrorCode::AlreadyExists, "A tag with this name already exists"));
        }
        return Result<void>::failure(toError(query.lastError()));
    }

    if (query.numRowsAffected() == 0)
    {
        return Result<void>::failure(Error(ErrorCode::NotFound, "No tag with this id"));
    }

    Logging::debug("SQLiteTagRepository: tag updated");
    return Result<void>::success();
}

Result<void> SQLiteTagRepository::deleteTag(Tag::Id id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM Tags WHERE id = ?"));
    query.addBindValue(static_cast<qint64>(id));

    if (!query.exec())
    {
        return Result<void>::failure(toError(query.lastError()));
    }

    if (query.numRowsAffected() == 0)
    {
        return Result<void>::failure(Error(ErrorCode::NotFound, "No tag with this id"));
    }

    Logging::debug("SQLiteTagRepository: tag deleted");
    return Result<void>::success();
}

Result<std::vector<Tag>> SQLiteTagRepository::allTags() const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, name, hex_color FROM Tags ORDER BY name"));

    if (!query.exec())
    {
        return Result<std::vector<Tag>>::failure(toError(query.lastError()));
    }

    std::vector<Tag> tags;
    while (query.next())
    {
        auto tag = Tag::create(query.value(0).toLongLong(), query.value(1).toString().toStdString(),
                                query.value(2).toString().toStdString());
        if (tag)
        {
            tags.push_back(std::move(tag).value());
        }
    }

    return Result<std::vector<Tag>>::success(std::move(tags));
}

Result<std::unordered_map<Tag::Id, int>> SQLiteTagRepository::tagFileCounts() const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT tag_id, COUNT(*) FROM FileTags GROUP BY tag_id"));

    if (!query.exec())
    {
        return Result<std::unordered_map<Tag::Id, int>>::failure(toError(query.lastError()));
    }

    std::unordered_map<Tag::Id, int> counts;
    while (query.next())
    {
        counts[query.value(0).toLongLong()] = query.value(1).toInt();
    }

    return Result<std::unordered_map<Tag::Id, int>>::success(std::move(counts));
}

Result<void> SQLiteTagRepository::mergeTags(Tag::Id sourceId, Tag::Id targetId)
{
    if (!m_db.transaction())
    {
        return Result<void>::failure(toError(m_db.lastError()));
    }

    QSqlQuery reassign(m_db);
    reassign.prepare(
        QStringLiteral("INSERT OR IGNORE INTO FileTags(file_id, tag_id) SELECT file_id, ? FROM FileTags WHERE tag_id = ?"));
    reassign.addBindValue(static_cast<qint64>(targetId));
    reassign.addBindValue(static_cast<qint64>(sourceId));
    if (!reassign.exec())
    {
        Error error = toError(reassign.lastError());
        m_db.rollback();
        return Result<void>::failure(std::move(error));
    }

    QSqlQuery deleteAssociations(m_db);
    deleteAssociations.prepare(QStringLiteral("DELETE FROM FileTags WHERE tag_id = ?"));
    deleteAssociations.addBindValue(static_cast<qint64>(sourceId));
    if (!deleteAssociations.exec())
    {
        Error error = toError(deleteAssociations.lastError());
        m_db.rollback();
        return Result<void>::failure(std::move(error));
    }

    QSqlQuery deleteTagQuery(m_db);
    deleteTagQuery.prepare(QStringLiteral("DELETE FROM Tags WHERE id = ?"));
    deleteTagQuery.addBindValue(static_cast<qint64>(sourceId));
    if (!deleteTagQuery.exec())
    {
        Error error = toError(deleteTagQuery.lastError());
        m_db.rollback();
        return Result<void>::failure(std::move(error));
    }

    if (!m_db.commit())
    {
        Error error = toError(m_db.lastError());
        m_db.rollback();
        return Result<void>::failure(std::move(error));
    }

    Logging::debug("SQLiteTagRepository: tags merged");
    return Result<void>::success();
}

Result<std::int64_t> SQLiteTagRepository::findOrCreateFileRow(const std::filesystem::path& path, std::optional<std::uint64_t> hash)
{
    const QString qPath = toQString(path);

    QSqlQuery byPath(m_db);
    byPath.prepare(QStringLiteral("SELECT id FROM Files WHERE path = ?"));
    byPath.addBindValue(qPath);
    if (!byPath.exec())
    {
        return Result<std::int64_t>::failure(toError(byPath.lastError()));
    }
    if (byPath.next())
    {
        return Result<std::int64_t>::success(byPath.value(0).toLongLong());
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // Hash-based rename-relocation only applies to rows with a real content hash — folders have
    // none (Architecture.md §8 hashing is file-only) and are identified by path alone, so a
    // hash-less caller skips straight to inserting a new row.
    if (hash.has_value())
    {
        const QString qHash = hashToText(hash.value());

        QSqlQuery byHash(m_db);
        byHash.prepare(QStringLiteral("SELECT id FROM Files WHERE file_hash = ?"));
        byHash.addBindValue(qHash);
        if (!byHash.exec())
        {
            return Result<std::int64_t>::failure(toError(byHash.lastError()));
        }
        if (byHash.next())
        {
            const std::int64_t existingId = byHash.value(0).toLongLong();

            QSqlQuery relocate(m_db);
            relocate.prepare(QStringLiteral("UPDATE Files SET path = ?, last_seen_at = ? WHERE id = ?"));
            relocate.addBindValue(qPath);
            relocate.addBindValue(now);
            relocate.addBindValue(existingId);
            if (!relocate.exec())
            {
                return Result<std::int64_t>::failure(toError(relocate.lastError()));
            }

            return Result<std::int64_t>::success(existingId);
        }
    }

    // A default-constructed QString() is "null" and Qt's SQLite driver binds a null QString as
    // SQL NULL, which would violate file_hash's NOT NULL constraint — bind an empty (non-null)
    // string instead for hash-less rows (folders).
    const QString qHash = hash.has_value() ? hashToText(hash.value()) : QStringLiteral("");

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral("INSERT INTO Files(path, file_hash, last_seen_at) VALUES(?, ?, ?)"));
    insert.addBindValue(qPath);
    insert.addBindValue(qHash);
    insert.addBindValue(now);
    if (!insert.exec())
    {
        return Result<std::int64_t>::failure(toError(insert.lastError()));
    }

    return Result<std::int64_t>::success(insert.lastInsertId().toLongLong());
}

Result<void> SQLiteTagRepository::assignTag(const FileTagAssociation& association)
{
    auto fileId = findOrCreateFileRow(association.filePath(), association.fileHash());
    if (!fileId)
    {
        return Result<void>::failure(std::move(fileId).error());
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO FileTags(file_id, tag_id) VALUES(?, ?)"));
    query.addBindValue(static_cast<qint64>(fileId.value()));
    query.addBindValue(static_cast<qint64>(association.tagId()));

    if (!query.exec())
    {
        if (isUniqueConstraintViolation(query.lastError()))
        {
            return Result<void>::failure(Error(ErrorCode::AlreadyExists, "File already has this tag"));
        }
        return Result<void>::failure(toError(query.lastError()));
    }

    Logging::debug("SQLiteTagRepository: tag assigned to file");
    return Result<void>::success();
}

Result<void> SQLiteTagRepository::unassignTag(const std::filesystem::path& filePath, Tag::Id tagId)
{
    QSqlQuery fileQuery(m_db);
    fileQuery.prepare(QStringLiteral("SELECT id FROM Files WHERE path = ?"));
    fileQuery.addBindValue(toQString(filePath));
    if (!fileQuery.exec())
    {
        return Result<void>::failure(toError(fileQuery.lastError()));
    }
    if (!fileQuery.next())
    {
        return Result<void>::failure(Error(ErrorCode::NotFound, "No such file is tracked"));
    }

    const qint64 fileId = fileQuery.value(0).toLongLong();

    QSqlQuery deleteQuery(m_db);
    deleteQuery.prepare(QStringLiteral("DELETE FROM FileTags WHERE file_id = ? AND tag_id = ?"));
    deleteQuery.addBindValue(fileId);
    deleteQuery.addBindValue(static_cast<qint64>(tagId));
    if (!deleteQuery.exec())
    {
        return Result<void>::failure(toError(deleteQuery.lastError()));
    }

    if (deleteQuery.numRowsAffected() == 0)
    {
        return Result<void>::failure(Error(ErrorCode::NotFound, "File does not have this tag"));
    }

    Logging::debug("SQLiteTagRepository: tag unassigned from file");
    return Result<void>::success();
}

Result<std::vector<Tag>> SQLiteTagRepository::tagsForFile(const std::filesystem::path& filePath) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"(
        SELECT t.id, t.name, t.hex_color
        FROM Tags t
        JOIN FileTags ft ON ft.tag_id = t.id
        JOIN Files f ON f.id = ft.file_id
        WHERE f.path = ?
        ORDER BY t.name
    )"));
    query.addBindValue(toQString(filePath));

    if (!query.exec())
    {
        return Result<std::vector<Tag>>::failure(toError(query.lastError()));
    }

    std::vector<Tag> tags;
    while (query.next())
    {
        auto tag = Tag::create(query.value(0).toLongLong(), query.value(1).toString().toStdString(),
                                query.value(2).toString().toStdString());
        if (tag)
        {
            tags.push_back(std::move(tag).value());
        }
    }

    return Result<std::vector<Tag>>::success(std::move(tags));
}

Result<TagsByPathMap> SQLiteTagRepository::tagsForFiles(const std::vector<std::filesystem::path>& filePaths) const
{
    if (filePaths.empty())
    {
        return Result<TagsByPathMap>::success({});
    }

    TagsByPathMap result;

    // Batched to stay under SQLite's compiled SQLITE_MAX_VARIABLE_NUMBER (see kTagsForFilesBatchSize)
    // -- a directory with more entries than one batch's worth issues multiple queries and merges
    // their results rather than binding every path's placeholder into one query.
    for (std::size_t offset = 0; offset < filePaths.size(); offset += kTagsForFilesBatchSize)
    {
        const std::size_t batchEnd = std::min(offset + static_cast<std::size_t>(kTagsForFilesBatchSize), filePaths.size());

        QStringList placeholders;
        for (std::size_t i = offset; i < batchEnd; ++i)
        {
            placeholders << QStringLiteral("?");
        }

        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(R"(
            SELECT f.path, t.id, t.name, t.hex_color
            FROM Tags t
            JOIN FileTags ft ON ft.tag_id = t.id
            JOIN Files f ON f.id = ft.file_id
            WHERE f.path IN (%1)
            ORDER BY f.path, t.name
        )")
                           .arg(placeholders.join(QStringLiteral(","))));
        for (std::size_t i = offset; i < batchEnd; ++i)
        {
            query.addBindValue(toQString(filePaths[i]));
        }

        if (!query.exec())
        {
            return Result<TagsByPathMap>::failure(toError(query.lastError()));
        }

        while (query.next())
        {
            const std::filesystem::path path = toPath(query.value(0).toString());
            auto tag = Tag::create(query.value(1).toLongLong(), query.value(2).toString().toStdString(),
                                    query.value(3).toString().toStdString());
            if (tag)
            {
                result[path].push_back(std::move(tag).value());
            }
        }
    }

    return Result<TagsByPathMap>::success(std::move(result));
}

Result<std::vector<FileTagAssociation>> SQLiteTagRepository::findFilesWithAllTags(const std::vector<Tag::Id>& tagIds) const
{
    if (tagIds.empty())
    {
        return Result<std::vector<FileTagAssociation>>::success({});
    }

    QStringList placeholders;
    for (std::size_t i = 0; i < tagIds.size(); ++i)
    {
        placeholders << QStringLiteral("?");
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"(
        SELECT f.path, f.file_hash
        FROM Files f
        JOIN FileTags ft ON ft.file_id = f.id
        WHERE ft.tag_id IN (%1)
        GROUP BY f.id
        HAVING COUNT(DISTINCT ft.tag_id) = ?
    )")
                       .arg(placeholders.join(QStringLiteral(","))));
    for (Tag::Id id : tagIds)
    {
        query.addBindValue(static_cast<qint64>(id));
    }
    query.addBindValue(static_cast<qint64>(tagIds.size()));

    if (!query.exec())
    {
        return Result<std::vector<FileTagAssociation>>::failure(toError(query.lastError()));
    }

    std::vector<FileTagAssociation> associations;
    while (query.next())
    {
        const std::filesystem::path path = toPath(query.value(0).toString());
        const std::uint64_t hash = query.value(1).toString().toULongLong();

        for (Tag::Id id : tagIds)
        {
            auto association = FileTagAssociation::create(path, hash, id);
            if (association)
            {
                associations.push_back(std::move(association).value());
            }
        }
    }

    return Result<std::vector<FileTagAssociation>>::success(std::move(associations));
}

Result<std::vector<FileTagAssociation>> SQLiteTagRepository::findFilesWithAnyOfTagGroups(
    const std::vector<std::vector<Tag::Id>>& tagIdGroups) const
{
    if (tagIdGroups.empty()
        || std::any_of(tagIdGroups.begin(), tagIdGroups.end(), [](const auto& group) { return group.empty(); }))
    {
        return Result<std::vector<FileTagAssociation>>::success({});
    }

    QStringList existsClauses;
    QStringList matchedTagClauses;
    for (const auto& group : tagIdGroups)
    {
        QStringList placeholders;
        for (std::size_t i = 0; i < group.size(); ++i)
        {
            placeholders << QStringLiteral("?");
        }
        const QString placeholderList = placeholders.join(QStringLiteral(","));
        existsClauses << QStringLiteral("EXISTS (SELECT 1 FROM FileTags ft WHERE ft.file_id = f.id AND ft.tag_id IN (%1))")
                             .arg(placeholderList);
        // Which member of this OR-group the file is actually tagged with -- any member can satisfy
        // the EXISTS clause above, so the reported tagId must come from the file's real FileTags
        // rows rather than an arbitrary group member (e.g. group.front()) that may not apply to it.
        matchedTagClauses << QStringLiteral(
                                  "(SELECT ft.tag_id FROM FileTags ft WHERE ft.file_id = f.id AND ft.tag_id IN (%1) LIMIT 1)")
                                 .arg(placeholderList);
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"(
        SELECT f.path, f.file_hash, %1
        FROM Files f
        WHERE %2
    )")
                       .arg(matchedTagClauses.join(QStringLiteral(", ")), existsClauses.join(QStringLiteral(" AND "))));
    for (const auto& group : tagIdGroups)
    {
        for (Tag::Id id : group)
        {
            query.addBindValue(static_cast<qint64>(id));
        }
    }
    for (const auto& group : tagIdGroups)
    {
        for (Tag::Id id : group)
        {
            query.addBindValue(static_cast<qint64>(id));
        }
    }

    if (!query.exec())
    {
        return Result<std::vector<FileTagAssociation>>::failure(toError(query.lastError()));
    }

    std::vector<FileTagAssociation> associations;
    while (query.next())
    {
        const std::filesystem::path path = toPath(query.value(0).toString());
        const std::uint64_t hash = query.value(1).toString().toULongLong();

        for (int groupIndex = 0; groupIndex < static_cast<int>(tagIdGroups.size()); ++groupIndex)
        {
            const Tag::Id matchedTagId = static_cast<Tag::Id>(query.value(2 + groupIndex).toLongLong());
            auto association = FileTagAssociation::create(path, hash, matchedTagId);
            if (association)
            {
                associations.push_back(std::move(association).value());
            }
        }
    }

    return Result<std::vector<FileTagAssociation>>::success(std::move(associations));
}
