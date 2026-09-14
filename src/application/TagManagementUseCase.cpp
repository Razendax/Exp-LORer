#include "TagManagementUseCase.h"

#include <algorithm>
#include <cctype>

namespace
{
    std::string toLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }

    // Ranks a tag name against a (already-lowercased) query: exact match ranks best, then prefix,
    // then substring; anything else does not match at all.
    enum class MatchRank
    {
        Exact,
        Prefix,
        Substring,
        None,
    };

    MatchRank rankMatch(const std::string& lowerName, const std::string& lowerQuery)
    {
        if (lowerName == lowerQuery)
        {
            return MatchRank::Exact;
        }
        if (lowerName.rfind(lowerQuery, 0) == 0)
        {
            return MatchRank::Prefix;
        }
        if (lowerName.find(lowerQuery) != std::string::npos)
        {
            return MatchRank::Substring;
        }
        return MatchRank::None;
    }
}

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

    // The partial-content hash (Architecture.md §8) only makes sense for regular files; folders
    // have no bytes to hash. A tagged folder is identified by path alone (findOrCreateFileRow
    // skips hash-based rename-relocation for hash-less rows) — acceptable, since only files need
    // the "robust handling of renames/moves" guarantee (Specification.md §2.2).
    std::optional<std::uint64_t> hash = file.hash();
    if (!hash.has_value() && !file.isDirectory())
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

Result<std::vector<Tag>> TagManagementUseCase::tagsForPathWithAncestors(const std::filesystem::path& path) const
{
    std::vector<std::filesystem::path> ancestors; // immediate parent first
    std::filesystem::path current = path;
    std::filesystem::path parent = current.parent_path();
    while (parent != current && !parent.empty())
    {
        ancestors.push_back(parent);
        current = parent;
        parent = current.parent_path();
    }
    std::reverse(ancestors.begin(), ancestors.end()); // outermost ancestor first

    std::vector<Tag> result;
    for (const auto& ancestor : ancestors)
    {
        auto tags = m_tagRepository.tagsForFile(ancestor);
        if (!tags)
        {
            return Result<std::vector<Tag>>::failure(std::move(tags).error());
        }
        result.insert(result.end(), tags.value().begin(), tags.value().end());
    }

    auto ownTags = m_tagRepository.tagsForFile(path);
    if (!ownTags)
    {
        return Result<std::vector<Tag>>::failure(std::move(ownTags).error());
    }
    result.insert(result.end(), ownTags.value().begin(), ownTags.value().end());

    return Result<std::vector<Tag>>::success(std::move(result));
}

Result<std::vector<Tag>> TagManagementUseCase::searchTags(const std::string& query) const
{
    auto all = m_tagRepository.allTags();
    if (!all)
    {
        return Result<std::vector<Tag>>::failure(std::move(all).error());
    }

    std::vector<Tag> tags = std::move(all).value();

    auto byNameAscending = [](const Tag& lhs, const Tag& rhs) { return toLower(lhs.name()) < toLower(rhs.name()); };

    if (query.empty())
    {
        std::sort(tags.begin(), tags.end(), byNameAscending);
        return Result<std::vector<Tag>>::success(std::move(tags));
    }

    const std::string lowerQuery = toLower(query);

    std::vector<Tag> matched;
    for (const auto& tag : tags)
    {
        if (rankMatch(toLower(tag.name()), lowerQuery) != MatchRank::None)
        {
            matched.push_back(tag);
        }
    }

    std::stable_sort(matched.begin(), matched.end(), [&lowerQuery](const Tag& lhs, const Tag& rhs) {
        const MatchRank rankLhs = rankMatch(toLower(lhs.name()), lowerQuery);
        const MatchRank rankRhs = rankMatch(toLower(rhs.name()), lowerQuery);
        if (rankLhs != rankRhs)
        {
            return rankLhs < rankRhs;
        }
        return toLower(lhs.name()) < toLower(rhs.name());
    });

    return Result<std::vector<Tag>>::success(std::move(matched));
}
