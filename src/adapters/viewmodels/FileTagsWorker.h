#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <QObject>

#include "FileNode.h"
#include "FileTagsByPath.h"
#include "Result.h"

class IFileSystemRepository;
class SQLiteTagRepository;
class TagManagementUseCase;

// Runs TagManagementUseCase::tagsForFiles on whatever thread it has been moved to. Owned and driven
// by FileTagsService, same shape as AdvancedSearchWorker.
//
// Unlike GitStatusWorker, this worker cannot simply borrow CompositionRoot's shared
// TagManagementUseCase: that use case wraps a QSqlDatabase connection which is thread-affine, and
// this worker's queries run on a thread other than the one that owns the shared connection. So it
// owns a second, dedicated SQLiteTagRepository connection to the same database file, opened lazily
// (on this worker's own thread, the first time it's actually needed) rather than in the constructor
// -- the constructor still runs on whatever thread constructs FileTagsService, before moveToThread
// has taken effect.
class FileTagsWorker : public QObject
{
    Q_OBJECT

public:
    FileTagsWorker(IFileSystemRepository& fileSystemRepository, const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~FileTagsWorker() override;

    Result<TagsByPathMap> fetchTags(const std::vector<FileNode>& files);

private:
    void ensureTagRepository();

    IFileSystemRepository& m_fileSystemRepository;
    std::filesystem::path m_databasePath;

    // This worker's own connection, constructed lazily on this thread (see class comment).
    std::unique_ptr<SQLiteTagRepository> m_tagRepository;
    std::unique_ptr<TagManagementUseCase> m_tagManagementUseCase;
};
