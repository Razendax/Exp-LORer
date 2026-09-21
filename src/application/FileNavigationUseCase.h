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
#include "SearchCriteria.h"

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

    // Excludes directories whenever either bound is set (a directory has no "size" in this app's
    // model); no-op passthrough when both bounds are nullopt.
    static std::vector<FileNode> filterBySizeRange(std::vector<FileNode> files,
                                                    std::optional<std::uintmax_t> minBytes,
                                                    std::optional<std::uintmax_t> maxBytes);

    // extensions are already-split/trimmed tokens (splitting is a UI/adapter concern, not this
    // layer's), OR'd together case-insensitively via the same toLower comparison as
    // filterByExtension. Excludes directories when the list is non-empty; no-op passthrough on an
    // empty vector.
    static std::vector<FileNode> filterByExtensions(std::vector<FileNode> files, const std::vector<std::string>& extensions);

    // Composes filterByName -> filterBySizeRange -> filterByExtensions, each a no-op when its part
    // of criteria is unset. The single entry point TabViewModel calls for advanced search
    // (Architecture.md §14.19).
    static std::vector<FileNode> filterByCriteria(std::vector<FileNode> files, const SearchCriteria& criteria);

    // Keeps only entries whose path() is present in allowedPaths (exact match — same path-keyed
    // posture as ITagRepository). Used to intersect a name/size/extension-filtered listing with
    // tag-search matches (Architecture.md §14.22). No-op passthrough is NOT implied by an empty
    // allowedPaths — callers only invoke this when tagIds is non-empty.
    static std::vector<FileNode> filterByPaths(std::vector<FileNode> files,
                                                const std::vector<std::filesystem::path>& allowedPaths);

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

private:
    IFileSystemRepository& m_fileSystemRepository;
    IContextMenuProvider& m_contextMenuProvider;
};
