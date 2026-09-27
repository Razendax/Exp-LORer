#include "FileTagsWorker.h"

#include "SQLiteTagRepository.h"
#include "TagManagementUseCase.h"

FileTagsWorker::FileTagsWorker(IFileSystemRepository& fileSystemRepository, const std::filesystem::path& databasePath, QObject* parent)
    : QObject(parent)
    , m_fileSystemRepository(fileSystemRepository)
    , m_databasePath(databasePath)
{
}

FileTagsWorker::~FileTagsWorker() = default;

void FileTagsWorker::ensureTagRepository()
{
    if (m_tagRepository)
    {
        return;
    }

    m_tagRepository = std::make_unique<SQLiteTagRepository>(m_databasePath);
    m_tagManagementUseCase = std::make_unique<TagManagementUseCase>(*m_tagRepository, m_fileSystemRepository);
}

Result<TagsByPathMap> FileTagsWorker::fetchTags(const std::vector<FileNode>& files)
{
    ensureTagRepository();
    return m_tagManagementUseCase->tagsForFiles(files);
}
