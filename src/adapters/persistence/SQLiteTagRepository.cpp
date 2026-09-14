#include "SQLiteTagRepository.h"

#include <filesystem>

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

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

    Error toError(const QSqlError& sqlError, ErrorCode fallback = ErrorCode::IoError)
    {
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
    migration.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS Tags (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL UNIQUE,
            hex_color   TEXT NOT NULL,
            created_at  INTEGER NOT NULL
        )
    )"));
    migration.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS Files (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            path          TEXT NOT NULL UNIQUE,
            file_hash     TEXT NOT NULL,
            size          INTEGER NOT NULL DEFAULT 0,
            modified_at   INTEGER NOT NULL DEFAULT 0,
            last_seen_at  INTEGER NOT NULL
        )
    )"));
    migration.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_files_hash ON Files(file_hash)"));
    migration.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS FileTags (
            file_id  INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,
            tag_id   INTEGER NOT NULL REFERENCES Tags(id) ON DELETE CASCADE,
            PRIMARY KEY (file_id, tag_id)
        )
    )"));
    migration.exec(QStringLiteral("PRAGMA user_version = 1"));
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
