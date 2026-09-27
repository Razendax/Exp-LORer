#pragma once

#include <string>
#include <vector>

#include "FileNode.h"
#include "FileTagAssociation.h"
#include "FileTagsByPath.h"
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

    // Bulk variant of tagsForFile for one directory listing's worth of entries (the Tags column,
    // Architecture.md §14.x). Skips synthetic rows (a set displayName(), e.g. "This PC"'s drives)
    // the same way GitStatusColumn's data() already does -- they have no real on-disk path to
    // look up.
    Result<TagsByPathMap> tagsForFiles(const std::vector<FileNode>& files) const;

    Result<std::vector<FileTagAssociation>> findFilesWithAllTags(const std::vector<Tag::Id>& tagIds) const;

    // Tags of every ancestor directory of `path` (outermost ancestor first, down to the
    // immediate parent) followed by path's own tags last. A root-level path has no ancestors.
    Result<std::vector<Tag>> tagsForPathWithAncestors(const std::filesystem::path& path) const;

    // Ranks allTags() against `query`: case-insensitive exact match, then prefix, then substring,
    // each group alphabetical by name. An empty query returns all tags, alphabetically.
    Result<std::vector<Tag>> searchTags(const std::string& query) const;

private:
    ITagRepository& m_tagRepository;
    IFileSystemRepository& m_fileSystemRepository;
};
