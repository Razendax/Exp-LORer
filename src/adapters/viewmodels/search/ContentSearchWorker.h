#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>

#include "ContentSearchMatch.h"
#include "ContentSearchOptions.h"
#include "FileNode.h"
#include "Result.h"

class ContentSearchUseCase;
class FileNavigationUseCase;
class IFileSystemRepository;
class LibGit2StatusProvider;
class GitStatusUseCase;

// Runs the I/O parts of "Find in Files" (Architecture.md §14.104) on whatever thread it has been
// moved to. Owned and driven by ContentSearchService, same shape as AdvancedSearchWorker but
// simpler: no tag-repository dependency at all, since content search has no tag integration.
class ContentSearchWorker : public QObject
{
    Q_OBJECT

public:
    ContentSearchWorker(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                         ContentSearchUseCase& contentSearchUseCase, QObject* parent = nullptr);
    ~ContentSearchWorker() override;

    Result<std::vector<RawFileLines>> scan(const std::filesystem::path& root, const std::string& includePattern,
                                            const TextMatchOptions& includeOptions, const std::string& excludePattern,
                                            const TextMatchOptions& excludeOptions, bool skipGitIgnored, bool showHidden,
                                            const std::function<void(const std::vector<FileNode>&)>& onProgress = nullptr) const;

private:
    // Lazily constructs m_gitStatusProvider/m_gitStatusUseCase on this worker's own thread, the
    // first time skipGitIgnored is actually used -- libgit2's git_repository* handles are
    // thread-affine, so this worker needs its own instances rather than sharing
    // GitStatusService/GitStatusWorker's, the same reasoning AdvancedSearchWorker's own
    // ensureGitStatusUseCase already documents. const (mutating only the mutable members below) so
    // it's reachable from scan(), which stays const to match AdvancedSearchWorker::scanRecursive's
    // own pre-existing signature.
    void ensureGitStatusUseCase() const;

    FileNavigationUseCase& m_fileNavigationUseCase;
    IFileSystemRepository& m_fileSystemRepository;
    ContentSearchUseCase& m_contentSearchUseCase;

    mutable std::unique_ptr<LibGit2StatusProvider> m_gitStatusProvider;
    mutable std::unique_ptr<GitStatusUseCase> m_gitStatusUseCase;
};
