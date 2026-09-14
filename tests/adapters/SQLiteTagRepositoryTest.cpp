#include <gtest/gtest.h>

#include "SQLiteTagRepository.h"

namespace
{
    class SQLiteTagRepositoryTest : public ::testing::Test
    {
    protected:
        SQLiteTagRepository m_repository{ std::filesystem::path(":memory:") };
    };
}

TEST_F(SQLiteTagRepositoryTest, CreateTagPersistsAndReturnsAssignedId)
{
    auto result = m_repository.createTag("Work", "#FF8800");

    ASSERT_TRUE(result.hasValue());
    EXPECT_NE(result.value().id(), Tag::kUnassignedId);
    EXPECT_EQ(result.value().name(), "Work");
    EXPECT_EQ(result.value().hexColor(), "#FF8800");
}

TEST_F(SQLiteTagRepositoryTest, CreateTagFailsOnDuplicateName)
{
    ASSERT_TRUE(m_repository.createTag("Work", "#FF8800").hasValue());

    auto result = m_repository.createTag("Work", "#00FF00");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::AlreadyExists);
}

TEST_F(SQLiteTagRepositoryTest, AllTagsReturnsAlphabeticalListing)
{
    m_repository.createTag("Zebra", "#FF0000");
    m_repository.createTag("Apple", "#00FF00");

    auto result = m_repository.allTags();

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 2u);
    EXPECT_EQ(result.value()[0].name(), "Apple");
    EXPECT_EQ(result.value()[1].name(), "Zebra");
}

TEST_F(SQLiteTagRepositoryTest, UpdateTagChangesNameAndColor)
{
    Tag tag = m_repository.createTag("Work", "#FF8800").value();
    auto renamed = Tag::create(tag.id(), "Personal", "#0000FF").value();

    auto result = m_repository.updateTag(renamed);

    ASSERT_TRUE(result.hasValue());
    auto all = m_repository.allTags();
    ASSERT_TRUE(all.hasValue());
    ASSERT_EQ(all.value().size(), 1u);
    EXPECT_EQ(all.value()[0].name(), "Personal");
    EXPECT_EQ(all.value()[0].hexColor(), "#0000FF");
}

TEST_F(SQLiteTagRepositoryTest, UpdateTagFailsForUnknownId)
{
    auto result = m_repository.updateTag(Tag::create(999, "Ghost", "#FFFFFF").value());

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(SQLiteTagRepositoryTest, DeleteTagRemovesIt)
{
    Tag tag = m_repository.createTag("Work", "#FF8800").value();

    auto result = m_repository.deleteTag(tag.id());

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(m_repository.allTags().value().empty());
}

TEST_F(SQLiteTagRepositoryTest, DeleteTagFailsForUnknownId)
{
    auto result = m_repository.deleteTag(999);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(SQLiteTagRepositoryTest, AssignTagCreatesFileRowAndAssociation)
{
    Tag tag = m_repository.createTag("Work", "#FF8800").value();
    auto association = FileTagAssociation::create("C:/data/report.pdf", 0xABCDu, tag.id()).value();

    auto result = m_repository.assignTag(association);

    ASSERT_TRUE(result.hasValue());
    auto tags = m_repository.tagsForFile("C:/data/report.pdf");
    ASSERT_TRUE(tags.hasValue());
    ASSERT_EQ(tags.value().size(), 1u);
    EXPECT_EQ(tags.value()[0].id(), tag.id());
}

TEST_F(SQLiteTagRepositoryTest, AssignTagFailsOnDuplicateAssociation)
{
    Tag tag = m_repository.createTag("Work", "#FF8800").value();
    auto association = FileTagAssociation::create("C:/data/report.pdf", 0xABCDu, tag.id()).value();
    ASSERT_TRUE(m_repository.assignTag(association).hasValue());

    auto result = m_repository.assignTag(association);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::AlreadyExists);
}

TEST_F(SQLiteTagRepositoryTest, AssignTagWithoutHashCreatesFileRowForFolderTagging)
{
    Tag tag = m_repository.createTag("Work", "#FF8800").value();
    auto association = FileTagAssociation::create("C:/data", std::nullopt, tag.id()).value();

    auto result = m_repository.assignTag(association);

    ASSERT_TRUE(result.hasValue());
    auto tags = m_repository.tagsForFile("C:/data");
    ASSERT_TRUE(tags.hasValue());
    ASSERT_EQ(tags.value().size(), 1u);
    EXPECT_EQ(tags.value()[0].id(), tag.id());
}

TEST_F(SQLiteTagRepositoryTest, UnassignTagRemovesAssociation)
{
    Tag tag = m_repository.createTag("Work", "#FF8800").value();
    auto association = FileTagAssociation::create("C:/data/report.pdf", 0xABCDu, tag.id()).value();
    ASSERT_TRUE(m_repository.assignTag(association).hasValue());

    auto result = m_repository.unassignTag("C:/data/report.pdf", tag.id());

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(m_repository.tagsForFile("C:/data/report.pdf").value().empty());
}

TEST_F(SQLiteTagRepositoryTest, UnassignTagFailsForUntrackedFile)
{
    auto result = m_repository.unassignTag("C:/data/missing.pdf", 1);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(SQLiteTagRepositoryTest, TagsForFileReturnsEmptyForUntrackedFile)
{
    auto result = m_repository.tagsForFile("C:/data/missing.pdf");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

TEST_F(SQLiteTagRepositoryTest, AssignTagRelocatesRowByHashOnRename)
{
    Tag tag = m_repository.createTag("Work", "#FF8800").value();
    auto original = FileTagAssociation::create("C:/data/old.pdf", 0xABCDu, tag.id()).value();
    ASSERT_TRUE(m_repository.assignTag(original).hasValue());

    Tag secondTag = m_repository.createTag("Personal", "#00FF00").value();
    auto renamed = FileTagAssociation::create("C:/data/new.pdf", 0xABCDu, secondTag.id()).value();
    auto result = m_repository.assignTag(renamed);

    ASSERT_TRUE(result.hasValue());

    // The relocated row now answers to the new path only, carrying both tags forward.
    auto tagsAtNewPath = m_repository.tagsForFile("C:/data/new.pdf");
    ASSERT_TRUE(tagsAtNewPath.hasValue());
    EXPECT_EQ(tagsAtNewPath.value().size(), 2u);

    auto tagsAtOldPath = m_repository.tagsForFile("C:/data/old.pdf");
    ASSERT_TRUE(tagsAtOldPath.hasValue());
    EXPECT_TRUE(tagsAtOldPath.value().empty());
}

TEST_F(SQLiteTagRepositoryTest, FindFilesWithAllTagsReturnsOnlyFilesHavingEveryTag)
{
    Tag work = m_repository.createTag("Work", "#FF8800").value();
    Tag urgent = m_repository.createTag("Urgent", "#FF0000").value();

    auto both = FileTagAssociation::create("C:/data/both.pdf", 0x1111u, work.id()).value();
    ASSERT_TRUE(m_repository.assignTag(both).hasValue());
    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/both.pdf", 0x1111u, urgent.id()).value())
                    .hasValue());

    auto onlyWork = FileTagAssociation::create("C:/data/onlyWork.pdf", 0x2222u, work.id()).value();
    ASSERT_TRUE(m_repository.assignTag(onlyWork).hasValue());

    auto result = m_repository.findFilesWithAllTags({ work.id(), urgent.id() });

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 2u); // one association per requested tag for the single matching file
    for (const auto& association : result.value())
    {
        EXPECT_EQ(association.filePath(), std::filesystem::path("C:/data/both.pdf"));
    }
}
