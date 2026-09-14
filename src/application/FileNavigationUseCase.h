#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "FileNode.h"
#include "IFileSystemRepository.h"
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
    explicit FileNavigationUseCase(IFileSystemRepository& fileSystemRepository);

    Result<std::vector<FileNode>> listDirectory(const std::filesystem::path& directory) const;
    Result<FileNode> stat(const std::filesystem::path& path) const;

    static std::vector<FileNode> sortBy(std::vector<FileNode> files, SortCriterion criterion, bool ascending = true);
    static std::vector<FileNode> filterByExtension(std::vector<FileNode> files, const std::string& extension);

    Result<FileNode> moveFile(const std::filesystem::path& source, const std::filesystem::path& destination);
    Result<FileNode> copyFile(const std::filesystem::path& source, const std::filesystem::path& destination);
    Result<void> moveFileToTrash(const std::filesystem::path& path);
    Result<void> deleteFilePermanently(const std::filesystem::path& path);
    Result<void> openFile(const std::filesystem::path& path);

private:
    IFileSystemRepository& m_fileSystemRepository;
};
