#include "AdvancedSearchWorker.h"

#include <unordered_set>

#include "FileNavigationUseCase.h"
#include "FileTagAssociation.h"
#include "GitStatusUseCase.h"
#include "LibGit2StatusProvider.h"
#include "PathContainment.h"
#include "PathHash.h"
#include "SQLiteTagRepository.h"
#include "TagManagementUseCase.h"

AdvancedSearchWorker::AdvancedSearchWorker(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                                            const std::filesystem::path& databasePath, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_fileSystemRepository(fileSystemRepository)
    , m_databasePath(databasePath)
{
}

AdvancedSearchWorker::~AdvancedSearchWorker() = default;

void AdvancedSearchWorker::ensureTagRepository()
{
    if (m_tagRepository)
    {
        return;
    }

    m_tagRepository = std::make_unique<SQLiteTagRepository>(m_databasePath);
    m_tagManagementUseCase = std::make_unique<TagManagementUseCase>(*m_tagRepository, m_fileSystemRepository);
}

void AdvancedSearchWorker::ensureGitStatusUseCase() const
{
    if (m_gitStatusUseCase)
    {
        return;
    }

    m_gitStatusProvider = std::make_unique<LibGit2StatusProvider>();
    m_gitStatusUseCase = std::make_unique<GitStatusUseCase>(*m_gitStatusProvider);
}

std::function<bool(const std::filesystem::path&)> AdvancedSearchWorker::buildGitIgnorePrunePredicate(
    const std::filesystem::path& root) const
{
    ensureGitStatusUseCase();

    auto ignoredResult = m_gitStatusUseCase->ignoredPaths(root);
    auto ignoredSet = std::make_shared<std::unordered_set<std::filesystem::path, PathHash>>();
    if (ignoredResult.hasValue())
    {
        ignoredSet->insert(ignoredResult.value().begin(), ignoredResult.value().end());
    }
    // Not reported as "ignored" by git's own status machinery (it isn't part of the working tree
    // at all), but a user skipping git-ignored content clearly doesn't want ".git"'s internals in
    // results either.
    ignoredSet->insert(root / ".git");

    return [ignoredSet](const std::filesystem::path& path) { return ignoredSet->count(path) != 0; };
}

Result<std::vector<FileNode>> AdvancedSearchWorker::scanRecursive(
    const std::filesystem::path& root, const std::function<void(const std::vector<FileNode>&)>& onProgress,
    bool skipGitIgnored) const
{
    const std::function<bool(const std::filesystem::path&)> shouldPrune =
        skipGitIgnored ? buildGitIgnorePrunePredicate(root) : nullptr;

    return m_fileNavigationUseCase.listDirectoryRecursive(root, onProgress, shouldPrune);
}

Result<std::vector<FileNode>> AdvancedSearchWorker::fetchTagFirstCandidates(const std::filesystem::path& root,
                                                                             const std::vector<Tag::Id>& tagIds)
{
    ensureTagRepository();

    return buildCandidatesFromAssociations(root, m_tagManagementUseCase->findFilesWithAllTags(tagIds));
}

Result<std::vector<FileNode>> AdvancedSearchWorker::fetchTagFirstCandidatesIncludingChildren(
    const std::filesystem::path& root, const std::vector<Tag::Id>& tagIds)
{
    ensureTagRepository();

    return buildCandidatesFromAssociations(root, m_tagManagementUseCase->findFilesWithTagsIncludingChildren(tagIds));
}

Result<std::vector<FileNode>> AdvancedSearchWorker::buildCandidatesFromAssociations(
    const std::filesystem::path& root, Result<std::vector<FileTagAssociation>> tagResult)
{
    if (tagResult.hasError())
    {
        return Result<std::vector<FileNode>>::failure(std::move(tagResult).error());
    }

    std::vector<FileNode> candidates;
    candidates.reserve(tagResult.value().size());
    for (const FileTagAssociation& association : tagResult.value())
    {
        if (!isPathWithinOrEqual(association.filePath(), root))
        {
            continue;
        }

        auto statResult = m_fileNavigationUseCase.stat(association.filePath());
        if (statResult.hasError())
        {
            // File moved/deleted/renamed since tagging -- no hash-based relocation
            // (Architecture.md §14.22), so it's silently dropped rather than surfaced as an error.
            continue;
        }

        candidates.push_back(std::move(statResult).value());
    }

    return Result<std::vector<FileNode>>::success(std::move(candidates));
}
