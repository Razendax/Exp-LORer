#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "FileTagAssociation.h"
#include "FileTagsByPath.h"
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

    // Bulk variant of tagsForFile for one directory's worth of paths at once (Architecture.md
    // §14.x, Tags column) -- one query instead of one round-trip per file. Paths with no tags are
    // simply absent from the returned map, not present with an empty vector.
    virtual Result<TagsByPathMap> tagsForFiles(const std::vector<std::filesystem::path>& filePaths) const = 0;

    // Files tagged with every id in tagIds (AND filtering, Architecture.md §7).
    virtual Result<std::vector<FileTagAssociation>> findFilesWithAllTags(const std::vector<Tag::Id>& tagIds) const = 0;

    // Files tagged with at least one id from every group in tagIdGroups (AND-of-OR; each group is
    // one originally-selected tag's own id plus its descendant ids — Architecture.md §14.87). Empty
    // outer vector = no filter, mirrors findFilesWithAllTags.
    virtual Result<std::vector<FileTagAssociation>> findFilesWithAnyOfTagGroups(
        const std::vector<std::vector<Tag::Id>>& tagIdGroups) const = 0;
};
