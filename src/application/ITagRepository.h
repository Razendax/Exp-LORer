#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "FileTagAssociation.h"
#include "Result.h"
#include "Tag.h"

// Port for persisting tags and file/tag associations (Architecture.md §2.2). Implemented by
// src/adapters/persistence/SQLiteTagRepository.
class ITagRepository
{
public:
    virtual ~ITagRepository() = default;

    virtual Result<Tag> createTag(const std::string& name, const std::string& hexColor) = 0;
    virtual Result<void> updateTag(const Tag& tag) = 0;
    virtual Result<void> deleteTag(Tag::Id id) = 0;
    virtual Result<std::vector<Tag>> allTags() const = 0;

    virtual Result<void> assignTag(const FileTagAssociation& association) = 0;
    virtual Result<void> unassignTag(const std::filesystem::path& filePath, Tag::Id tagId) = 0;
    virtual Result<std::vector<Tag>> tagsForFile(const std::filesystem::path& filePath) const = 0;

    // Files tagged with every id in tagIds (AND filtering, Architecture.md §7).
    virtual Result<std::vector<FileTagAssociation>> findFilesWithAllTags(const std::vector<Tag::Id>& tagIds) const = 0;
};
