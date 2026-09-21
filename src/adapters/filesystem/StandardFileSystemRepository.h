#pragma once

#include <chrono>

#include "ArchiveIndexCache.h"
#include "ArchivePathResolver.h"
#include "IFileSystemRepository.h"

// Implements IFileSystemRepository using std::filesystem (Architecture.md §2.3). Windows long
// paths (>260 chars) are handled via the \\?\ extended-length prefix internally; callers never
// see the prefix in a returned FileNode's path. OS trash uses SHFileOperationW on Windows.
//
// Archive browsing (Architecture.md §14.28): every method below re-resolves its path(s) via
// ArchivePathResolver first and branches to the archive-aware implementation when the path is
// inside a real, on-disk archive file -- real filesystem paths are entirely unaffected.
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

    Result<void> extractArchive(const std::filesystem::path& archiveFile,
                                 const std::filesystem::path& destinationDirectory) override;

    Result<std::filesystem::path> materializeForReading(const std::filesystem::path& path) const override;

private:
    // Synthesizes the "This PC" listing (quick-access folders + drives) instead of touching disk.
    // Windows-only; see Architecture.md §14.15. Non-Windows returns an IoError "Not supported on
    // this platform" failure, matching openWithDefaultApplication's existing posture.
    Result<std::vector<FileNode>> listThisPc() const;

    Result<std::vector<FileNode>> listArchiveDirectory(const std::filesystem::path& directory,
                                                        const ArchivePathResolver::Resolution& resolution) const;
    Result<FileNode> statArchiveEntry(const std::filesystem::path& outwardPath,
                                       const ArchivePathResolver::Resolution& resolution) const;
    Result<void> openArchiveEntryWithDefaultApplication(const ArchivePathResolver::Resolution& resolution);

    // Shared by openArchiveEntryWithDefaultApplication/materializeForReading: extracts the
    // resolved entry into a freshly-named private temp directory and returns that directory (not
    // cleaned up by the app -- relies on normal OS temp-dir lifecycle, Architecture.md §14.28), so
    // repeated calls for the same/different entries never collide.
    Result<std::filesystem::path> extractEntryToTempDir(const ArchivePathResolver::Resolution& resolution) const;

    // Shared by listDirectory/stat: the archive's own modification time, as a fallback
    // last-write time for synthesized (directory-only, e.g. zip) entries. Epoch on failure.
    std::chrono::system_clock::time_point archiveModificationTime(const std::filesystem::path& archiveFile) const;

    mutable ArchiveIndexCache m_archiveIndexCache;
};
