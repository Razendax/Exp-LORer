#include "FileNode.h"

Result<FileNode> FileNode::create(std::filesystem::path path,
                                   std::uintmax_t size,
                                   std::chrono::system_clock::time_point creationDate,
                                   std::chrono::system_clock::time_point modificationDate,
                                   FileType fileType,
                                   bool isHidden,
                                   std::optional<std::uint64_t> hash)
{
    if (path.empty())
    {
        return Result<FileNode>::failure(Error(ErrorCode::InvalidArgument, "FileNode path must not be empty"));
    }

    return Result<FileNode>::success(
        FileNode(std::move(path), size, creationDate, modificationDate, fileType, isHidden, hash));
}

FileNode::FileNode(std::filesystem::path path,
                    std::uintmax_t size,
                    std::chrono::system_clock::time_point creationDate,
                    std::chrono::system_clock::time_point modificationDate,
                    FileType fileType,
                    bool isHidden,
                    std::optional<std::uint64_t> hash,
                    std::optional<std::string> displayName)
    : m_path(std::move(path))
    , m_size(size)
    , m_creationDate(creationDate)
    , m_modificationDate(modificationDate)
    , m_fileType(fileType)
    , m_isHidden(isHidden)
    , m_hash(hash)
    , m_displayName(std::move(displayName))
{
}

FileNode FileNode::withHash(std::uint64_t hash) const
{
    return FileNode(m_path, m_size, m_creationDate, m_modificationDate, m_fileType, m_isHidden, hash, m_displayName);
}

FileNode FileNode::withDisplayName(std::string displayName) const
{
    return FileNode(
        m_path, m_size, m_creationDate, m_modificationDate, m_fileType, m_isHidden, m_hash, std::move(displayName));
}

bool operator==(const FileNode& lhs, const FileNode& rhs) noexcept
{
    return lhs.m_path == rhs.m_path && lhs.m_size == rhs.m_size && lhs.m_creationDate == rhs.m_creationDate
        && lhs.m_modificationDate == rhs.m_modificationDate && lhs.m_fileType == rhs.m_fileType
        && lhs.m_isHidden == rhs.m_isHidden && lhs.m_hash == rhs.m_hash && lhs.m_displayName == rhs.m_displayName;
}
