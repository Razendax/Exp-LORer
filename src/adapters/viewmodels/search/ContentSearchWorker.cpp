#include "ContentSearchWorker.h"

#include "ContentSearchUseCase.h"
#include "GitIgnorePrunePredicateBuilder.h"
#include "GitStatusUseCase.h"
#include "LibGit2StatusProvider.h"

ContentSearchWorker::ContentSearchWorker(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                                          ContentSearchUseCase& contentSearchUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_fileSystemRepository(fileSystemRepository)
    , m_contentSearchUseCase(contentSearchUseCase)
{
}

ContentSearchWorker::~ContentSearchWorker() = default;

void ContentSearchWorker::ensureGitStatusUseCase() const
{
    if (m_gitStatusUseCase)
    {
        return;
    }

    m_gitStatusProvider = std::make_unique<LibGit2StatusProvider>();
    m_gitStatusUseCase = std::make_unique<GitStatusUseCase>(*m_gitStatusProvider);
}

Result<std::vector<RawFileLines>> ContentSearchWorker::scan(
    const std::filesystem::path& root, const std::string& includePattern, const TextMatchOptions& includeOptions,
    const std::string& excludePattern, const TextMatchOptions& excludeOptions, bool skipGitIgnored, bool showHidden,
    const std::function<void(const std::vector<FileNode>&)>& onProgress) const
{
    std::function<bool(const std::filesystem::path&)> shouldPrune;
    if (skipGitIgnored)
    {
        ensureGitStatusUseCase();
        shouldPrune = buildGitIgnorePrunePredicate(*m_gitStatusUseCase, root);
    }

    return m_contentSearchUseCase.collectSearchableContent(root, shouldPrune, showHidden, includePattern, includeOptions,
                                                            excludePattern, excludeOptions, onProgress);
}
