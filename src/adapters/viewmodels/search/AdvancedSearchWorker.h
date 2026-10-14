#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QObject>

#include "FileNode.h"
#include "FileTagAssociation.h"
#include "Result.h"
#include "Tag.h"

class FileNavigationUseCase;
class IFileSystemRepository;
class SQLiteTagRepository;
class TagManagementUseCase;
class LibGit2StatusProvider;
class GitStatusUseCase;

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
    //
    // skipGitIgnored, when true, prunes the walk to skip everything .gitignore would exclude under
    // `root` (Architecture.md §14.45) -- the quick Search tab's "skip git-ignored" toggle; the
    // Advanced Search pane's own call site leaves this at its default (no equivalent toggle there).
    Result<std::vector<FileNode>> scanRecursive(const std::filesystem::path& root,
                                                 const std::function<void(const std::vector<FileNode>&)>& onProgress = nullptr,
                                                 bool skipGitIgnored = false) const;

    // Tag-first candidate fetch -- DB query + per-path stat() (Architecture.md §14.22 steps 1-3).
    // Returns the *candidate* FileNodes; filterByCriteria/sortBy stay on the calling thread (pure,
    // cheap, no I/O -- no reason to move them here).
    Result<std::vector<FileNode>> fetchTagFirstCandidates(const std::filesystem::path& root, const std::vector<Tag::Id>& tagIds);

    // Like fetchTagFirstCandidates, but widens each requested tag to itself plus its "/"-nested
    // children before querying (Architecture.md §14.87) -- the Advanced Search pane's
    // Exact/Include-Children toggle, set to "include children".
    Result<std::vector<FileNode>> fetchTagFirstCandidatesIncludingChildren(const std::filesystem::path& root,
                                                                            const std::vector<Tag::Id>& tagIds);

    // Every file/folder tagged with exactly tagId, with no folder-root scoping and no "including
    // children" widening (Architecture.md §14.89) -- the Tag Manager dialog's per-tag results list,
    // which has no folder context since it's a global, tab-independent dialog.
    Result<std::vector<FileNode>> fetchFilesForTag(Tag::Id tagId);

private:
    void ensureTagRepository();

    // Shared post-processing for fetchTagFirstCandidates*/fetchFilesForTag: when `root` is set,
    // filters associations to those within it; std::nullopt skips the containment filter entirely
    // (Architecture.md §14.89 -- fetchFilesForTag has no folder root to scope to). Then stats each
    // surviving path, silently dropping files moved/deleted since tagging (Architecture.md §14.22).
    Result<std::vector<FileNode>> buildCandidatesFromAssociations(
        const std::optional<std::filesystem::path>& root, Result<std::vector<FileTagAssociation>> tagResult);

    // Lazily constructs m_gitStatusProvider/m_gitStatusUseCase on this worker's own thread, the
    // first time skipGitIgnored is actually used -- libgit2's git_repository* handles are
    // thread-affine (never touch the same handle from two threads at once), so this worker needs
    // its own instances rather than sharing GitStatusService/GitStatusWorker's, the same reasoning
    // that already governs m_tagRepository/ensureTagRepository above (Architecture.md §14.45).
    // const (mutating only the mutable members below) so it's reachable from scanRecursive, which
    // stays const to match its own pre-existing signature.
    void ensureGitStatusUseCase() const;

    // Builds the shouldPrune predicate scanRecursive passes to
    // FileNavigationUseCase::listDirectoryRecursive: every path ignoredPaths(root) reports, plus
    // root/".git" itself (git's own status machinery never reports ".git" as "ignored" since it
    // isn't part of the working tree at all, but a user skipping ignored content clearly doesn't
    // want ".git"'s internals in results either). Tolerates ignoredPaths() failure/empty (not a
    // repository) by returning an always-false predicate, i.e. an unrestricted scan.
    std::function<bool(const std::filesystem::path&)> buildGitIgnorePrunePredicate(const std::filesystem::path& root) const;

    FileNavigationUseCase& m_fileNavigationUseCase;
    IFileSystemRepository& m_fileSystemRepository;
    std::filesystem::path m_databasePath;

    // This worker's own connection, constructed lazily on this thread (see class comment).
    std::unique_ptr<SQLiteTagRepository> m_tagRepository;
    std::unique_ptr<TagManagementUseCase> m_tagManagementUseCase;

    // This worker's own libgit2 instances, constructed lazily on this thread (see
    // ensureGitStatusUseCase). mutable so scanRecursive can stay const.
    mutable std::unique_ptr<LibGit2StatusProvider> m_gitStatusProvider;
    mutable std::unique_ptr<GitStatusUseCase> m_gitStatusUseCase;
};
