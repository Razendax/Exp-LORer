#pragma once

#include "IFileSystemRepository.h"

// Implements IFileSystemRepository using std::filesystem (Architecture.md §2.3). Windows long
// paths (>260 chars) are handled via the \\?\ extended-length prefix internally; callers never
// see the prefix in a returned FileNode's path. OS trash uses SHFileOperationW on Windows.
class StandardFileSystemRepository : public IFileSystemRepository
{
public:
    Result<std::vector<FileNode>> listDirectory(const std::filesystem::path& directory) const override;
    Result<std::vector<FileNode>> listDirectoryRecursive(const std::filesystem::path& root) const override;
    Result<FileNode> stat(const std::filesystem::path& path) const override;

    Result<FileNode> move(const std::filesystem::path& source, const std::filesystem::path& destination) override;
    Result<FileNode> copy(const std::filesystem::path& source, const std::filesystem::path& destination) override;

    Result<void> moveToTrash(const std::filesystem::path& path) override;
    Result<void> deletePermanently(const std::filesystem::path& path) override;
    Result<void> openWithDefaultApplication(const std::filesystem::path& path) override;

    Result<std::uint64_t> computeFileHash(const FileNode& file) const override;

    Result<void> createDirectory(const std::filesystem::path& directory) override;
    Result<FileNode> createFileFromTemplate(const std::filesystem::path& destinationFile,
                                             const std::optional<std::filesystem::path>& templateFile) override;
    Result<void> showProperties(const std::filesystem::path& path, NativeWindowHandle ownerWindow) override;

    Result<std::vector<std::byte>> readFilePrefix(const std::filesystem::path& path, std::size_t maxBytes) const override;

private:
    // Synthesizes the "This PC" listing (quick-access folders + drives) instead of touching disk.
    // Windows-only; see Architecture.md §14.15. Non-Windows returns an IoError "Not supported on
    // this platform" failure, matching openWithDefaultApplication's existing posture.
    Result<std::vector<FileNode>> listThisPc() const;
};
