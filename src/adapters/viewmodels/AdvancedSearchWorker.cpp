#include "AdvancedSearchWorker.h"

#include "FileNavigationUseCase.h"
#include "FileTagAssociation.h"
#include "PathContainment.h"
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

Result<std::vector<FileNode>> AdvancedSearchWorker::scanRecursive(
    const std::filesystem::path& root, const std::function<void(const std::vector<FileNode>&)>& onProgress) const
{
    return m_fileNavigationUseCase.listDirectoryRecursive(root, onProgress);
}

Result<std::vector<FileNode>> AdvancedSearchWorker::fetchTagFirstCandidates(const std::filesystem::path& root,
                                                                             const std::vector<Tag::Id>& tagIds)
{
    ensureTagRepository();

    auto tagResult = m_tagManagementUseCase->findFilesWithAllTags(tagIds);
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
