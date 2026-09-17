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

FileNavigationUseCase::FileNavigationUseCase(IFileSystemRepository& fileSystemRepository, IContextMenuProvider& contextMenuProvider)
    : m_fileSystemRepository(fileSystemRepository)
    , m_contextMenuProvider(contextMenuProvider)
{
}

Result<std::vector<FileNode>> FileNavigationUseCase::listDirectory(const std::filesystem::path& directory) const
{
    return m_fileSystemRepository.listDirectory(directory);
}

Result<std::vector<FileNode>> FileNavigationUseCase::listDirectoryRecursive(const std::filesystem::path& root) const
{
    return m_fileSystemRepository.listDirectoryRecursive(root);
}

Result<FileNode> FileNavigationUseCase::stat(const std::filesystem::path& path) const
{
    return m_fileSystemRepository.stat(path);
}

std::vector<FileNode> FileNavigationUseCase::sortBy(std::vector<FileNode> files, SortCriterion criterion, bool ascending)
{
    auto less = [criterion](const FileNode& lhs, const FileNode& rhs) {
        if (lhs.isDirectory() != rhs.isDirectory())
        {
            // Directories are grouped before files; reversing the whole comparator for
            // descending order (below) flips this alongside the criterion, so folders lead in
            // ascending order and files lead in descending order.
            return lhs.isDirectory();
        }

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

std::vector<FileNode> FileNavigationUseCase::filterByName(std::vector<FileNode> files, const std::string& query)
{
    if (query.empty())
    {
        return files;
    }

    const std::string wanted = toLower(query);

    std::vector<FileNode> result;
    std::copy_if(files.begin(), files.end(), std::back_inserter(result), [&wanted](const FileNode& file) {
        return toLower(file.name().string()).find(wanted) != std::string::npos;
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

Result<void> FileNavigationUseCase::openFile(const std::filesystem::path& path)
{
    return m_fileSystemRepository.openWithDefaultApplication(path);
}

Result<std::vector<ContextMenuEntry>> FileNavigationUseCase::buildItemContextMenu(
    const std::vector<std::filesystem::path>& paths, ContextMenuSourceMode mode)
{
    return m_contextMenuProvider.buildItemMenu(paths, mode);
}

Result<std::vector<ContextMenuEntry>> FileNavigationUseCase::buildBackgroundContextMenu(const std::filesystem::path& folder,
                                                                                          ContextMenuSourceMode mode)
{
    return m_contextMenuProvider.buildBackgroundMenu(folder, mode);
}

Result<void> FileNavigationUseCase::invokeContextMenuEntry(std::uint32_t entryId, NativeWindowHandle ownerWindow)
{
    return m_contextMenuProvider.invoke(entryId, ownerWindow);
}

void FileNavigationUseCase::discardContextMenu()
{
    m_contextMenuProvider.discardMenu();
}

Result<void> FileNavigationUseCase::createFolder(const std::filesystem::path& directory)
{
    return m_fileSystemRepository.createDirectory(directory);
}

Result<FileNode> FileNavigationUseCase::createFileFromTemplate(const std::filesystem::path& destinationFile,
                                                                 const std::optional<std::filesystem::path>& templateFile)
{
    return m_fileSystemRepository.createFileFromTemplate(destinationFile, templateFile);
}

Result<void> FileNavigationUseCase::showProperties(const std::filesystem::path& path, NativeWindowHandle ownerWindow)
{
    return m_fileSystemRepository.showProperties(path, ownerWindow);
}
