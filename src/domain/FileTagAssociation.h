#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "Result.h"
#include "Tag.h"

// Entity mapping a FileNode's hash/path to a Tag's id (Architecture.md §2.1). The path is the
// last known location; fileHash is the fallback identity used to relocate a row after a
// rename/move (Architecture.md §7 "Path resolution").
class FileTagAssociation
{
public:
    static Result<FileTagAssociation> create(std::filesystem::path filePath,
                                               std::optional<std::uint64_t> fileHash,
                                               Tag::Id tagId);

    const std::filesystem::path& filePath() const noexcept { return m_filePath; }
    const std::optional<std::uint64_t>& fileHash() const noexcept { return m_fileHash; }
    Tag::Id tagId() const noexcept { return m_tagId; }

    friend bool operator==(const FileTagAssociation& lhs, const FileTagAssociation& rhs) noexcept;

private:
    FileTagAssociation(std::filesystem::path filePath, std::optional<std::uint64_t> fileHash, Tag::Id tagId);

    std::filesystem::path m_filePath;
    std::optional<std::uint64_t> m_fileHash;
    Tag::Id m_tagId;
};

inline bool operator!=(const FileTagAssociation& lhs, const FileTagAssociation& rhs) noexcept
{
    return !(lhs == rhs);
}
