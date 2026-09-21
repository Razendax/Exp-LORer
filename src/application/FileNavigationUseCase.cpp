#include "FileNavigationUseCase.h"

#include <algorithm>
#include <cctype>
#include <set>

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

    // Splits a comma/space-separated extension list (e.g. "jpg, png") into trimmed tokens, each
    // normalized to include a leading '.' so it compares directly against
    // std::filesystem::path::extension(). Empty tokens are dropped.
    std::vector<std::string> splitExtensionList(const std::string& extensionList)
    {
        std::vector<std::string> tokens;
        std::string current;

        auto flush = [&]() {
            const std::size_t begin = current.find_first_not_of(" \t");
            const std::size_t end = current.find_last_not_of(" \t");
            if (begin == std::string::npos)
            {
                current.clear();
                return;
            }
            std::string trimmed = current.substr(begin, end - begin + 1);
            if (!trimmed.empty() && trimmed.front() != '.')
            {
                trimmed.insert(trimmed.begin(), '.');
            }
            tokens.push_back(std::move(trimmed));
            current.clear();
        };

        for (char c : extensionList)
        {
            if (c == ',' || c == ' ' || c == '\t')
            {
                flush();
            }
            else
            {
                current.push_back(c);
            }
        }
        flush();

        return tokens;
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
                return toLower(PathUtf8::toUtf8(lhs.name())) < toLower(PathUtf8::toUtf8(rhs.name()));
            case SortCriterion::Size:
                return lhs.size() < rhs.size();
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

    const std::string wanted = toLower(query);

    std::vector<FileNode> result;
    std::copy_if(files.begin(), files.end(), std::back_inserter(result), [&wanted](const FileNode& file) {
        return toLower(PathUtf8::toUtf8(file.name())).find(wanted) != std::string::npos;
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

std::vector<FileNode> FileNavigationUseCase::filterByExtensions(std::vector<FileNode> files,
                                                                   const std::vector<std::string>& extensions)
{
    if (extensions.empty())
    {
        return files;
    }

    std::vector<std::string> wanted;
    wanted.reserve(extensions.size());
    for (const std::string& extension : extensions)
    {
        wanted.push_back(toLower(extension));
    }

    std::vector<FileNode> result;
    std::copy_if(files.begin(), files.end(), std::back_inserter(result), [&](const FileNode& file) {
        if (file.isDirectory())
        {
            return false;
        }
        const std::string extension = toLower(PathUtf8::toUtf8(file.path().extension()));
        return std::find(wanted.begin(), wanted.end(), extension) != wanted.end();
    });

    return result;
}

std::vector<FileNode> FileNavigationUseCase::filterByCriteria(std::vector<FileNode> files, const SearchCriteria& criteria)
{
    files = filterByName(std::move(files), criteria.nameQuery);
    files = filterBySizeRange(std::move(files), criteria.minSizeBytes, criteria.maxSizeBytes);
    files = filterByExtensions(std::move(files), splitExtensionList(criteria.extensionList));
    return files;
}

std::vector<FileNode> FileNavigationUseCase::filterByPaths(std::vector<FileNode> files,
                                                             const std::vector<std::filesystem::path>& allowedPaths)
{
    const std::set<std::filesystem::path> allowed(allowedPaths.begin(), allowedPaths.end());

    std::vector<FileNode> result;
    std::copy_if(files.begin(), files.end(), std::back_inserter(result),
                 [&allowed](const FileNode& file) { return allowed.contains(file.path()); });

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
