#pragma once

#include <string>
#include <vector>

#include "FileNode.h"
#include "FileTagAssociation.h"
#include "IFileSystemRepository.h"
#include "ITagRepository.h"
#include "Result.h"
#include "Tag.h"

// Creates/updates/deletes tags and assigns/unassigns them to files (Architecture.md §2.2,
// Scenario B). Resolves a file's identity hash on demand when assigning a tag so associations
// survive renames/moves. Stateless: a single shared instance may serve concurrent callers
// (Architecture.md §14.2).
class TagManagementUseCase
{
public:
    TagManagementUseCase(ITagRepository& tagRepository, IFileSystemRepository& fileSystemRepository);

    Result<Tag> createTag(const std::string& name, const std::string& hexColor);
    Result<void> updateTag(const Tag& tag);
    Result<void> deleteTag(Tag::Id id);
    Result<std::vector<Tag>> allTags() const;

    // Fails with ErrorCode::AlreadyExists if the file already carries tagId.
    Result<void> assignTag(const FileNode& file, Tag::Id tagId);
    Result<void> unassignTag(const FileNode& file, Tag::Id tagId);
    Result<std::vector<Tag>> tagsForFile(const FileNode& file) const;
    Result<std::vector<FileTagAssociation>> findFilesWithAllTags(const std::vector<Tag::Id>& tagIds) const;

private:
    ITagRepository& m_tagRepository;
    IFileSystemRepository& m_fileSystemRepository;
};
