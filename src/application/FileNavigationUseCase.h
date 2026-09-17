#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "FileNode.h"
#include "IContextMenuProvider.h"
#include "IFileSystemRepository.h"
#include "NativeTypes.h"
#include "Result.h"

enum class SortCriterion
{
    Name,
    Size,
    ModificationDate,
    FileType,
};

// Fetches directory contents, sorts/filters them, and performs basic CRUD file operations
// (Architecture.md §2.2). Stateless: a single shared instance may serve concurrent callers
// (Architecture.md §14.2).
class FileNavigationUseCase
{
public:
    FileNavigationUseCase(IFileSystemRepository& fileSystemRepository, IContextMenuProvider& contextMenuProvider);

    Result<std::vector<FileNode>> listDirectory(const std::filesystem::path& directory) const;
    Result<std::vector<FileNode>> listDirectoryRecursive(const std::filesystem::path& root) const;
    Result<FileNode> stat(const std::filesystem::path& path) const;

    static std::vector<FileNode> sortBy(std::vector<FileNode> files, SortCriterion criterion, bool ascending = true);
    static std::vector<FileNode> filterByExtension(std::vector<FileNode> files, const std::string& extension);
    static std::vector<FileNode> filterByName(std::vector<FileNode> files, const std::string& query);

    Result<FileNode> moveFile(const std::filesystem::path& source, const std::filesystem::path& destination);
    Result<FileNode> copyFile(const std::filesystem::path& source, const std::filesystem::path& destination);
    Result<void> moveFileToTrash(const std::filesystem::path& path);
    Result<void> deleteFilePermanently(const std::filesystem::path& path);
    Result<void> openFile(const std::filesystem::path& path);

    Result<std::vector<ContextMenuEntry>> buildItemContextMenu(const std::vector<std::filesystem::path>& paths,
                                                                 ContextMenuSourceMode mode);
    Result<std::vector<ContextMenuEntry>> buildBackgroundContextMenu(const std::filesystem::path& folder,
                                                                       ContextMenuSourceMode mode);
    Result<void> invokeContextMenuEntry(std::uint32_t entryId, NativeWindowHandle ownerWindow);
    void discardContextMenu();

    Result<void> createFolder(const std::filesystem::path& directory);
    Result<FileNode> createFileFromTemplate(const std::filesystem::path& destinationFile,
                                             const std::optional<std::filesystem::path>& templateFile);
    Result<void> showProperties(const std::filesystem::path& path, NativeWindowHandle ownerWindow);

private:
    IFileSystemRepository& m_fileSystemRepository;
    IContextMenuProvider& m_contextMenuProvider;
};
