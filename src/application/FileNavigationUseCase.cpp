#include "FileNavigationUseCase.h"

#include <algorithm>
#include <cctype>

#include "GlobMatch.h"
#include "PathUtf8.h"

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

Result<std::vector<FileNode>> FileNavigationUseCase::listDirectoryRecursive(
    const std::filesystem::path& root, const std::function<void(const std::vector<FileNode>&)>& onProgress) const
{
    return m_fileSystemRepository.listDirectoryRecursive(root, onProgress);
}

Result<FileNode> FileNavigationUseCase::stat(const std::filesystem::path& path) const
{
    return m_fileSystemRepository.stat(path);
}

std::vector<FileNode> FileNavigationUseCase::sortBy(std::vector<FileNode> files, SortCriterion criterion, bool ascending,
                                                      const std::function<std::uintmax_t(const FileNode&)>& sizeOf)
{
    const auto effectiveSize = [&sizeOf](const FileNode& node) {
        return sizeOf ? sizeOf(node) : node.size();
    };

    auto less = [criterion, &effectiveSize](const FileNode& lhs, const FileNode& rhs) {
        // Size/Percent order files and folders together by their byte count, so the usual
        // "directories first" grouping below would be misleading for them and is skipped.
        const bool groupDirectoriesFirst = criterion != SortCriterion::Size && criterion != SortCriterion::Percent;
        if (groupDirectoriesFirst && lhs.isDirectory() != rhs.isDirectory())
        {
            // Directories are grouped before files; reversing the whole comparator for
            // descending order (below) flips this alongside the criterion, so folders lead in
            // ascending order and files lead in descending order.
            return lhs.isDirectory();
        }

        switch (criterion)
        {
            case SortCriterion::Name:
                return toLower(PathUtf8::toUtf8(lhs.name())) < toLower(PathUtf8::toUtf8(rhs.name()));
            case SortCriterion::Size:
            case SortCriterion::Percent:
                return effectiveSize(lhs) < effectiveSize(rhs);
            case SortCriterion::ModificationDate:
                return lhs.modificationDate() < rhs.modificationDate();
            case SortCriterion::FileType:
                return toLower(PathUtf8::toUtf8(lhs.path().extension())) < toLower(PathUtf8::toUtf8(rhs.path().extension()));
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
        return toLower(PathUtf8::toUtf8(file.path().extension())) == wanted;
    });

    return result;
}

std::vector<FileNode> FileNavigationUseCase::filterByName(std::vector<FileNode> files, const std::string& query)
{
    if (query.empty())
    {
        return files;
    }

    const bool hasWildcard = query.find('*') != std::string::npos || query.find('?') != std::string::npos;
    const std::string pattern = hasWildcard ? query : "*" + query + "*";

    std::vector<FileNode> result;
    std::copy_if(files.begin(), files.end(), std::back_inserter(result), [&pattern](const FileNode& file) {
        return GlobMatch::matches(pattern, PathUtf8::toUtf8(file.name()));
    });

    return result;
}

std::vector<FileNode> FileNavigationUseCase::filterBySizeRange(std::vector<FileNode> files,
                                                                 std::optional<std::uintmax_t> minBytes,
                                                                 std::optional<std::uintmax_t> maxBytes)
{
    if (!minBytes && !maxBytes)
    {
        return files;
    }

    std::vector<FileNode> result;
    std::copy_if(files.begin(), files.end(), std::back_inserter(result), [&](const FileNode& file) {
        if (file.isDirectory())
        {
            return false;
        }
        if (minBytes && file.size() < *minBytes)
        {
            return false;
        }
        if (maxBytes && file.size() > *maxBytes)
        {
            return false;
        }
        return true;
    });

    return result;
}

std::vector<FileNode> FileNavigationUseCase::filterByCriteria(std::vector<FileNode> files, const SearchCriteria& criteria)
{
    files = filterByName(std::move(files), criteria.nameQuery);
    files = filterBySizeRange(std::move(files), criteria.minSizeBytes, criteria.maxSizeBytes);
    return files;
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

Result<void> FileNavigationUseCase::extractArchive(const std::filesystem::path& archiveFile,
                                                     const std::filesystem::path& destinationDirectory)
{
    return m_fileSystemRepository.extractArchive(archiveFile, destinationDirectory);
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

Result<DriveSpaceInfo> FileNavigationUseCase::driveSpaceInfo(const std::filesystem::path& path) const
{
    return m_fileSystemRepository.driveSpaceInfo(path);
}
