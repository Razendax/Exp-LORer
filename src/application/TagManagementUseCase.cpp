#include "TagManagementUseCase.h"

#include <algorithm>

TagManagementUseCase::TagManagementUseCase(ITagRepository& tagRepository, IFileSystemRepository& fileSystemRepository)
    : m_tagRepository(tagRepository)
    , m_fileSystemRepository(fileSystemRepository)
{
}

Result<Tag> TagManagementUseCase::createTag(const std::string& name, const std::string& hexColor)
{
    // Validate up front so the repository never receives a malformed name/color, even though
    // the persisted Tag::create() call happens repository-side once an id is assigned.
    auto validated = Tag::create(Tag::kUnassignedId, name, hexColor);
    if (!validated)
    {
        return Result<Tag>::failure(std::move(validated).error());
    }

    return m_tagRepository.createTag(name, hexColor);
}

Result<void> TagManagementUseCase::updateTag(const Tag& tag)
{
    if (tag.id() == Tag::kUnassignedId)
    {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "Cannot update a tag without a persisted id"));
    }

    return m_tagRepository.updateTag(tag);
}

Result<void> TagManagementUseCase::deleteTag(Tag::Id id)
{
    if (id == Tag::kUnassignedId)
    {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "Cannot delete a tag without a persisted id"));
    }

    return m_tagRepository.deleteTag(id);
}

Result<std::vector<Tag>> TagManagementUseCase::allTags() const
{
    return m_tagRepository.allTags();
}

Result<void> TagManagementUseCase::assignTag(const FileNode& file, Tag::Id tagId)
{
    auto existingTags = m_tagRepository.tagsForFile(file.path());
    if (!existingTags)
    {
        return Result<void>::failure(std::move(existingTags).error());
    }

    const bool alreadyTagged = std::any_of(existingTags.value().begin(), existingTags.value().end(),
                                            [tagId](const Tag& tag) { return tag.id() == tagId; });
    if (alreadyTagged)
    {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "File already has this tag"));
    }

    std::optional<std::uint64_t> hash = file.hash();
    if (!hash.has_value())
    {
        auto computedHash = m_fileSystemRepository.computeFileHash(file);
        if (!computedHash)
        {
            return Result<void>::failure(std::move(computedHash).error());
        }
        hash = computedHash.value();
    }

    auto association = FileTagAssociation::create(file.path(), hash, tagId);
    if (!association)
    {
        return Result<void>::failure(std::move(association).error());
    }

    return m_tagRepository.assignTag(association.value());
}

Result<void> TagManagementUseCase::unassignTag(const FileNode& file, Tag::Id tagId)
{
    return m_tagRepository.unassignTag(file.path(), tagId);
}

Result<std::vector<Tag>> TagManagementUseCase::tagsForFile(const FileNode& file) const
{
    return m_tagRepository.tagsForFile(file.path());
}

Result<std::vector<FileTagAssociation>> TagManagementUseCase::findFilesWithAllTags(const std::vector<Tag::Id>& tagIds) const
{
    return m_tagRepository.findFilesWithAllTags(tagIds);
}
