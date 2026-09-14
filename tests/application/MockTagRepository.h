#pragma once

#include <gmock/gmock.h>

#include "ITagRepository.h"

class MockTagRepository : public ITagRepository
{
public:
    MOCK_METHOD(Result<Tag>, createTag, (const std::string& name, const std::string& hexColor), (override));
    MOCK_METHOD(Result<void>, updateTag, (const Tag& tag), (override));
    MOCK_METHOD(Result<void>, deleteTag, (Tag::Id id), (override));
    MOCK_METHOD(Result<std::vector<Tag>>, allTags, (), (const, override));
    MOCK_METHOD(Result<void>, assignTag, (const FileTagAssociation& association), (override));
    MOCK_METHOD(Result<void>, unassignTag, (const std::filesystem::path& filePath, Tag::Id tagId), (override));
    MOCK_METHOD(Result<std::vector<Tag>>, tagsForFile, (const std::filesystem::path& filePath), (const, override));
    MOCK_METHOD(Result<std::vector<FileTagAssociation>>, findFilesWithAllTags, (const std::vector<Tag::Id>& tagIds), (const, override));
};
