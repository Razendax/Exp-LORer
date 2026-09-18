#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "Result.h"

enum class FileType
{
    Regular,
    Directory,
    Symlink,
    Other,
};

// Entity representing a file or directory. Identity for rename/move tracking is the
// partial content hash (size + first/last 64KB + mtime via xxHash64, see Architecture.md §8),
// not the path, which is why hash is a separate optional field rather than derived from path.
class FileNode
{
public:
    static Result<FileNode> create(std::filesystem::path path,
                                    std::uintmax_t size,
                                    std::chrono::system_clock::time_point creationDate,
                                    std::chrono::system_clock::time_point modificationDate,
                                    FileType fileType,
                                    bool isHidden = false,
                                    std::optional<std::uint64_t> hash = std::nullopt);

    const std::filesystem::path& path() const noexcept { return m_path; }
    std::filesystem::path name() const { return m_path.filename(); }
    std::uintmax_t size() const noexcept { return m_size; }
    std::chrono::system_clock::time_point creationDate() const noexcept { return m_creationDate; }
    std::chrono::system_clock::time_point modificationDate() const noexcept { return m_modificationDate; }
    FileType fileType() const noexcept { return m_fileType; }
    bool isHidden() const noexcept { return m_isHidden; }
    const std::optional<std::uint64_t>& hash() const noexcept { return m_hash; }
    const std::optional<std::string>& displayName() const noexcept { return m_displayName; }

    bool isDirectory() const noexcept { return m_fileType == FileType::Directory; }

    FileNode withHash(std::uint64_t hash) const;

    // Overrides name() for synthetic entries whose path() has no filename component (e.g. drive
    // roots like "C:\", whose filename() is empty per std::filesystem) — used by the "This PC"
    // virtual location (Architecture.md §14.15).
    FileNode withDisplayName(std::string displayName) const;

    friend bool operator==(const FileNode& lhs, const FileNode& rhs) noexcept;

private:
    FileNode(std::filesystem::path path,
              std::uintmax_t size,
              std::chrono::system_clock::time_point creationDate,
              std::chrono::system_clock::time_point modificationDate,
              FileType fileType,
              bool isHidden,
              std::optional<std::uint64_t> hash,
              std::optional<std::string> displayName = std::nullopt);

    std::filesystem::path m_path;
    std::uintmax_t m_size;
    std::chrono::system_clock::time_point m_creationDate;
    std::chrono::system_clock::time_point m_modificationDate;
    FileType m_fileType;
    bool m_isHidden;
    std::optional<std::uint64_t> m_hash;
    std::optional<std::string> m_displayName;
};

inline bool operator!=(const FileNode& lhs, const FileNode& rhs) noexcept
{
    return !(lhs == rhs);
}
