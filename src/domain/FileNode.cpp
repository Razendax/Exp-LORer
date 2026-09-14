#include "FileNode.h"

Result<FileNode> FileNode::create(std::filesystem::path path,
                                   std::uintmax_t size,
                                   std::chrono::system_clock::time_point creationDate,
                                   std::chrono::system_clock::time_point modificationDate,
                                   FileType fileType,
                                   std::optional<std::uint64_t> hash)
{
    if (path.empty())
    {
        return Result<FileNode>::failure(Error(ErrorCode::InvalidArgument, "FileNode path must not be empty"));
    }

    return Result<FileNode>::success(
        FileNode(std::move(path), size, creationDate, modificationDate, fileType, hash));
}

FileNode::FileNode(std::filesystem::path path,
                    std::uintmax_t size,
                    std::chrono::system_clock::time_point creationDate,
                    std::chrono::system_clock::time_point modificationDate,
                    FileType fileType,
                    std::optional<std::uint64_t> hash)
    : m_path(std::move(path))
    , m_size(size)
    , m_creationDate(creationDate)
    , m_modificationDate(modificationDate)
    , m_fileType(fileType)
    , m_hash(hash)
{
}

FileNode FileNode::withHash(std::uint64_t hash) const
{
    return FileNode(m_path, m_size, m_creationDate, m_modificationDate, m_fileType, hash);
}

bool operator==(const FileNode& lhs, const FileNode& rhs) noexcept
{
    return lhs.m_path == rhs.m_path && lhs.m_size == rhs.m_size && lhs.m_creationDate == rhs.m_creationDate
        && lhs.m_modificationDate == rhs.m_modificationDate && lhs.m_fileType == rhs.m_fileType
        && lhs.m_hash == rhs.m_hash;
}
