#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "DriveSpaceInfo.h"
#include "FileNode.h"
#include "IContextMenuProvider.h"
#include "IFileSystemRepository.h"
#include "NativeTypes.h"
#include "Result.h"
#include "SearchCriteria.h"

enum class SortCriterion
{
    Name,
    Size,
    ModificationDate,
    FileType,
    Percent,
};

// Fetches directory contents, sorts/filters them, and performs basic CRUD file operations
// (Architecture.md §2.2). Stateless: a single shared instance may serve concurrent callers
// (Architecture.md §14.2).
class FileNavigationUseCase
{
public:
    FileNavigationUseCase(IFileSystemRepository& fileSystemRepository, IContextMenuProvider& contextMenuProvider);

    Result<std::vector<FileNode>> listDirectory(const std::filesystem::path& directory) const;

    // onProgress forwards to IFileSystemRepository::listDirectoryRecursive -- see its doc comment
    // for the ~1/second throttling guarantee.
    Result<std::vector<FileNode>> listDirectoryRecursive(
        const std::filesystem::path& root, const std::function<void(const std::vector<FileNode>&)>& onProgress = nullptr) const;
    Result<FileNode> stat(const std::filesystem::path& path) const;

    // `sizeOf`, when set, overrides FileNode::size() for SortCriterion::Size/Percent comparisons --
    // a directory's FileNode::size() is always 0 (Architecture.md: directories aren't recursively
    // sized at listing time), so callers that know a directory's actual recursive size (e.g.
    // FileListModel's cached "Scan Folder"/"Scan Drive" results, Architecture.md §14.39) can supply
    // it here to sort directories by that value instead of leaving them all tied at 0.
    static std::vector<FileNode> sortBy(std::vector<FileNode> files, SortCriterion criterion, bool ascending = true,
                                          const std::function<std::uintmax_t(const FileNode&)>& sizeOf = {});
    static std::vector<FileNode> filterByExtension(std::vector<FileNode> files, const std::string& extension);

    // Empty query is a no-op passthrough. A query containing no '*'/'?' keeps the historical
    // "contains" behavior (wrapped as "*query*" internally); a query containing either is matched
    // as a full anchored glob against the whole name via GlobMatch (Architecture.md §14.19/§14.29).
    static std::vector<FileNode> filterByName(std::vector<FileNode> files, const std::string& query);

    // Excludes directories whenever either bound is set (a directory has no "size" in this app's
    // model); no-op passthrough when both bounds are nullopt.
    static std::vector<FileNode> filterBySizeRange(std::vector<FileNode> files,
                                                    std::optional<std::uintmax_t> minBytes,
                                                    std::optional<std::uintmax_t> maxBytes);

    // Composes filterByName -> filterBySizeRange, each a no-op when its part of criteria is unset.
    // The single entry point TabViewModel calls for advanced search (Architecture.md §14.19).
    static std::vector<FileNode> filterByCriteria(std::vector<FileNode> files, const SearchCriteria& criteria);

    Result<FileNode> moveFile(const std::filesystem::path& source, const std::filesystem::path& destination);
    Result<FileNode> copyFile(const std::filesystem::path& source, const std::filesystem::path& destination);
    Result<void> moveFileToTrash(const std::filesystem::path& path);
    Result<void> deleteFilePermanently(const std::filesystem::path& path);
    Result<void> openFile(const std::filesystem::path& path);

    // Read-only extraction (Architecture.md §14.28): every entry under archiveFile (or, if
    // archiveFile names a folder inside an archive, every entry under that subtree) into
    // destinationDirectory, preserving relative structure.
    Result<void> extractArchive(const std::filesystem::path& archiveFile, const std::filesystem::path& destinationDirectory);

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

    Result<DriveSpaceInfo> driveSpaceInfo(const std::filesystem::path& path) const;

private:
    IFileSystemRepository& m_fileSystemRepository;
    IContextMenuProvider& m_contextMenuProvider;
};
