#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>

#include "FileNode.h"
#include "Result.h"
#include "Tag.h"

class FileNavigationUseCase;
class IFileSystemRepository;
class SQLiteTagRepository;
class TagManagementUseCase;

// Runs the I/O parts of TabViewModel::runAdvancedSearch (Architecture.md §14.19/§14.22) on
// whatever thread it has been moved to. Owned and driven by AdvancedSearchService, same shape as
// GitStatusWorker/FilePreviewWorker.
//
// Unlike GitStatusWorker, this worker cannot simply borrow CompositionRoot's shared
// TagManagementUseCase: that use case wraps a QSqlDatabase connection which is thread-affine, and
// this worker's tag-first queries run on a thread other than the one that owns the shared
// connection. So it owns a second, dedicated SQLiteTagRepository connection to the same database
// file, opened lazily (on this worker's own thread, the first time it's actually needed) rather
// than in the constructor -- the constructor still runs on whatever thread constructs
// AdvancedSearchService, before moveToThread has taken effect.
class AdvancedSearchWorker : public QObject
{
    Q_OBJECT

public:
    AdvancedSearchWorker(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                          const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~AdvancedSearchWorker() override;

    // Recursive folder walk -- the non-tag branch's I/O (Architecture.md §14.19). onProgress
    // forwards to FileNavigationUseCase::listDirectoryRecursive; it is called on this worker's own
    // thread, so AdvancedSearchService is responsible for marshalling it back to the caller's
    // thread (Architecture.md §14.37).
    Result<std::vector<FileNode>> scanRecursive(const std::filesystem::path& root,
                                                 const std::function<void(const std::vector<FileNode>&)>& onProgress = nullptr) const;

    // Tag-first candidate fetch -- DB query + per-path stat() (Architecture.md §14.22 steps 1-3).
    // Returns the *candidate* FileNodes; filterByCriteria/sortBy stay on the calling thread (pure,
    // cheap, no I/O -- no reason to move them here).
    Result<std::vector<FileNode>> fetchTagFirstCandidates(const std::filesystem::path& root, const std::vector<Tag::Id>& tagIds);

private:
    void ensureTagRepository();

    FileNavigationUseCase& m_fileNavigationUseCase;
    IFileSystemRepository& m_fileSystemRepository;
    std::filesystem::path m_databasePath;

    // This worker's own connection, constructed lazily on this thread (see class comment).
    std::unique_ptr<SQLiteTagRepository> m_tagRepository;
    std::unique_ptr<TagManagementUseCase> m_tagManagementUseCase;
};
