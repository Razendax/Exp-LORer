#pragma once

#include <cstdint>
#include <optional>

#include <QSqlDatabase>
#include <QString>

#include "ITagRepository.h"

// Implements ITagRepository using QSqlDatabase/QSQLITE (Architecture.md §7, §10). Runs the
// Tags/Files/FileTags schema via a PRAGMA user_version check on construction. UTF-8/QString
// conversion for paths happens only at this boundary, per Architecture.md §6.
class SQLiteTagRepository : public ITagRepository
{
public:
    // `databasePath` may be ":memory:" for tests (Architecture.md §11). Its parent directory is
    // created if missing (e.g. the app-data location on first run).
    explicit SQLiteTagRepository(const std::filesystem::path& databasePath);
    ~SQLiteTagRepository() override;

    SQLiteTagRepository(const SQLiteTagRepository&) = delete;
    SQLiteTagRepository& operator=(const SQLiteTagRepository&) = delete;

    Result<Tag> createTag(const std::string& name, const std::string& hexColor) override;
    Result<void> updateTag(const Tag& tag) override;
    Result<void> deleteTag(Tag::Id id) override;
    Result<std::vector<Tag>> allTags() const override;

    Result<void> assignTag(const FileTagAssociation& association) override;
    Result<void> unassignTag(const std::filesystem::path& filePath, Tag::Id tagId) override;
    Result<std::vector<Tag>> tagsForFile(const std::filesystem::path& filePath) const override;

    Result<std::vector<FileTagAssociation>> findFilesWithAllTags(const std::vector<Tag::Id>& tagIds) const override;

private:
    void runMigrations();

    // Looks up the Files row for `path`; if missing and `hash` resolves an existing row (a
    // rename/move relocation, Architecture.md §7), updates that row's path and returns its id;
    // otherwise inserts a new row. Used by assignTag, which is the only place a file's identity
    // needs to be persisted/resolved.
    Result<std::int64_t> findOrCreateFileRow(const std::filesystem::path& path, std::optional<std::uint64_t> hash);

    QSqlDatabase m_db;
    QString m_connectionName;
};
