#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

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
                                    std::optional<std::uint64_t> hash = std::nullopt);

    const std::filesystem::path& path() const noexcept { return m_path; }
    std::filesystem::path name() const { return m_path.filename(); }
    std::uintmax_t size() const noexcept { return m_size; }
    std::chrono::system_clock::time_point creationDate() const noexcept { return m_creationDate; }
    std::chrono::system_clock::time_point modificationDate() const noexcept { return m_modificationDate; }
    FileType fileType() const noexcept { return m_fileType; }
    const std::optional<std::uint64_t>& hash() const noexcept { return m_hash; }

    bool isDirectory() const noexcept { return m_fileType == FileType::Directory; }

    FileNode withHash(std::uint64_t hash) const;

    friend bool operator==(const FileNode& lhs, const FileNode& rhs) noexcept;

private:
    FileNode(std::filesystem::path path,
              std::uintmax_t size,
              std::chrono::system_clock::time_point creationDate,
              std::chrono::system_clock::time_point modificationDate,
              FileType fileType,
              std::optional<std::uint64_t> hash);

    std::filesystem::path m_path;
    std::uintmax_t m_size;
    std::chrono::system_clock::time_point m_creationDate;
    std::chrono::system_clock::time_point m_modificationDate;
    FileType m_fileType;
    std::optional<std::uint64_t> m_hash;
};

inline bool operator!=(const FileNode& lhs, const FileNode& rhs) noexcept
{
    return !(lhs == rhs);
}
