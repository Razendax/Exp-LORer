#include "FileNavigationUseCase.h"

#include <algorithm>
#include <cctype>

namespace
{
    std::string toLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }
}

FileNavigationUseCase::FileNavigationUseCase(IFileSystemRepository& fileSystemRepository)
    : m_fileSystemRepository(fileSystemRepository)
{
}

Result<std::vector<FileNode>> FileNavigationUseCase::listDirectory(const std::filesystem::path& directory) const
{
    return m_fileSystemRepository.listDirectory(directory);
}

std::vector<FileNode> FileNavigationUseCase::sortBy(std::vector<FileNode> files, SortCriterion criterion, bool ascending)
{
    auto less = [criterion](const FileNode& lhs, const FileNode& rhs) {
        switch (criterion)
        {
            case SortCriterion::Name:
                return lhs.name() < rhs.name();
            case SortCriterion::Size:
                return lhs.size() < rhs.size();
            case SortCriterion::ModificationDate:
                return lhs.modificationDate() < rhs.modificationDate();
            case SortCriterion::FileType:
                return lhs.fileType() < rhs.fileType();
        }
        return false;
    };

    if (ascending)
    {
        std::stable_sort(files.begin(), files.end(), less);
    }
    else
    {
        std::stable_sort(files.begin(), files.end(), [&less](const FileNode& lhs, const FileNode& rhs) {
            return less(rhs, lhs);
        });
    }

    return files;
}

std::vector<FileNode> FileNavigationUseCase::filterByExtension(std::vector<FileNode> files, const std::string& extension)
{
    const std::string wanted = toLower(extension);

    std::vector<FileNode> result;
    std::copy_if(files.begin(), files.end(), std::back_inserter(result), [&wanted](const FileNode& file) {
        return toLower(file.path().extension().string()) == wanted;
    });

    return result;
}

Result<FileNode> FileNavigationUseCase::moveFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    return m_fileSystemRepository.move(source, destination);
}

Result<FileNode> FileNavigationUseCase::copyFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    return m_fileSystemRepository.copy(source, destination);
}

Result<void> FileNavigationUseCase::moveFileToTrash(const std::filesystem::path& path)
{
    return m_fileSystemRepository.moveToTrash(path);
}

Result<void> FileNavigationUseCase::deleteFilePermanently(const std::filesystem::path& path)
{
    return m_fileSystemRepository.deletePermanently(path);
}
