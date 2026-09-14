#include "FileTagAssociation.h"

Result<FileTagAssociation> FileTagAssociation::create(std::filesystem::path filePath,
                                                        std::optional<std::uint64_t> fileHash,
                                                        Tag::Id tagId)
{
    if (filePath.empty())
    {
        return Result<FileTagAssociation>::failure(
            Error(ErrorCode::InvalidArgument, "FileTagAssociation filePath must not be empty"));
    }

    if (tagId == Tag::kUnassignedId)
    {
        return Result<FileTagAssociation>::failure(
            Error(ErrorCode::InvalidArgument, "FileTagAssociation tagId must refer to a persisted Tag"));
    }

    return Result<FileTagAssociation>::success(FileTagAssociation(std::move(filePath), fileHash, tagId));
}

FileTagAssociation::FileTagAssociation(std::filesystem::path filePath, std::optional<std::uint64_t> fileHash, Tag::Id tagId)
    : m_filePath(std::move(filePath))
    , m_fileHash(fileHash)
    , m_tagId(tagId)
{
}

bool operator==(const FileTagAssociation& lhs, const FileTagAssociation& rhs) noexcept
{
    return lhs.m_filePath == rhs.m_filePath && lhs.m_fileHash == rhs.m_fileHash && lhs.m_tagId == rhs.m_tagId;
}
