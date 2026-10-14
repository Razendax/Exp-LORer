#include <algorithm>

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

TEST_F(SQLiteTagRepositoryTest, TagsForFilesReturnsMapForFilesWithTagsOnly)
{
    Tag work = m_repository.createTag("Work", "#FF8800").value();
    Tag urgent = m_repository.createTag("Urgent", "#FF0000").value();

    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/tagged.pdf", 0x1111u, work.id()).value())
                    .hasValue());
    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/tagged.pdf", 0x1111u, urgent.id()).value())
                    .hasValue());

    auto result = m_repository.tagsForFiles({ "C:/data/tagged.pdf", "C:/data/untagged.pdf", "C:/data/missing.pdf" });

    ASSERT_TRUE(result.hasValue());
    const auto& map = result.value();
    ASSERT_EQ(map.size(), 1u);
    const auto tagged = map.find("C:/data/tagged.pdf");
    ASSERT_NE(tagged, map.end());
    EXPECT_EQ(tagged->second.size(), 2u);
    EXPECT_EQ(map.find("C:/data/untagged.pdf"), map.end());
    EXPECT_EQ(map.find("C:/data/missing.pdf"), map.end());
}

TEST_F(SQLiteTagRepositoryTest, TagsForFilesReturnsEmptyMapForEmptyInput)
{
    auto result = m_repository.tagsForFiles({});

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
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

TEST_F(SQLiteTagRepositoryTest, FindFilesWithAnyOfTagGroupsAndsAcrossGroupsOrsWithinAGroup)
{
    Tag game = m_repository.createTag("game", "#FF8800").value();
    Tag action = m_repository.createTag("game/action", "#00FF00").value();
    Tag urgent = m_repository.createTag("Urgent", "#FF0000").value();

    // Matches group {game, action} via "action", and group {urgent} via "urgent".
    auto matching = FileTagAssociation::create("C:/data/matching.pdf", 0x1111u, action.id()).value();
    ASSERT_TRUE(m_repository.assignTag(matching).hasValue());
    ASSERT_TRUE(
        m_repository.assignTag(FileTagAssociation::create("C:/data/matching.pdf", 0x1111u, urgent.id()).value())
            .hasValue());

    // Satisfies only the first group, not the second.
    auto onlyGame = FileTagAssociation::create("C:/data/onlyGame.pdf", 0x2222u, game.id()).value();
    ASSERT_TRUE(m_repository.assignTag(onlyGame).hasValue());

    auto result = m_repository.findFilesWithAnyOfTagGroups({ { game.id(), action.id() }, { urgent.id() } });

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 2u); // one association per group for the single matching file
    for (const auto& association : result.value())
    {
        EXPECT_EQ(association.filePath(), std::filesystem::path("C:/data/matching.pdf"));
    }
}

TEST_F(SQLiteTagRepositoryTest, FindFilesWithAnyOfTagGroupsWithSingleIdGroupsMatchesExactAndSemantics)
{
    Tag work = m_repository.createTag("Work", "#FF8800").value();
    Tag urgent = m_repository.createTag("Urgent", "#FF0000").value();

    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/both.pdf", 0x1111u, work.id()).value())
                    .hasValue());
    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/both.pdf", 0x1111u, urgent.id()).value())
                    .hasValue());
    ASSERT_TRUE(
        m_repository.assignTag(FileTagAssociation::create("C:/data/onlyWork.pdf", 0x2222u, work.id()).value())
            .hasValue());

    auto result = m_repository.findFilesWithAnyOfTagGroups({ { work.id() }, { urgent.id() } });

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 2u);
    for (const auto& association : result.value())
    {
        EXPECT_EQ(association.filePath(), std::filesystem::path("C:/data/both.pdf"));
    }
}

TEST_F(SQLiteTagRepositoryTest, FindFilesWithAnyOfTagGroupsWithEmptyGroupYieldsZeroRows)
{
    Tag work = m_repository.createTag("Work", "#FF8800").value();
    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/tagged.pdf", 0x1111u, work.id()).value())
                    .hasValue());

    auto result = m_repository.findFilesWithAnyOfTagGroups({ { work.id() }, {} });

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

TEST_F(SQLiteTagRepositoryTest, TagFileCountsCountsOnlyTaggedFilesPerTag)
{
    Tag work = m_repository.createTag("Work", "#FF8800").value();
    Tag urgent = m_repository.createTag("Urgent", "#FF0000").value();

    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/a.pdf", 0x1111u, work.id()).value())
                    .hasValue());
    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/b.pdf", 0x2222u, work.id()).value())
                    .hasValue());
    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/b.pdf", 0x2222u, urgent.id()).value())
                    .hasValue());

    auto result = m_repository.tagFileCounts();

    ASSERT_TRUE(result.hasValue());
    const auto& counts = result.value();
    EXPECT_EQ(counts.at(work.id()), 2);
    EXPECT_EQ(counts.at(urgent.id()), 1);
}

TEST_F(SQLiteTagRepositoryTest, TagFileCountsOmitsTagsWithNoFiles)
{
    Tag unused = m_repository.createTag("Unused", "#00FF00").value();

    auto result = m_repository.tagFileCounts();

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().find(unused.id()), result.value().end());
}

TEST_F(SQLiteTagRepositoryTest, MergeTagsReassignsFilesAndDeletesSourceTag)
{
    Tag source = m_repository.createTag("Old", "#FF8800").value();
    Tag target = m_repository.createTag("New", "#00FF00").value();

    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/a.pdf", 0x1111u, source.id()).value())
                    .hasValue());

    auto result = m_repository.mergeTags(source.id(), target.id());

    ASSERT_TRUE(result.hasValue());

    auto tags = m_repository.tagsForFile("C:/data/a.pdf");
    ASSERT_TRUE(tags.hasValue());
    ASSERT_EQ(tags.value().size(), 1u);
    EXPECT_EQ(tags.value()[0].id(), target.id());

    auto allTags = m_repository.allTags();
    ASSERT_TRUE(allTags.hasValue());
    EXPECT_TRUE(std::none_of(allTags.value().begin(), allTags.value().end(),
                             [&](const Tag& tag) { return tag.id() == source.id(); }));
}

TEST_F(SQLiteTagRepositoryTest, MergeTagsIgnoresFilesAlreadyCarryingBothTags)
{
    Tag source = m_repository.createTag("Old", "#FF8800").value();
    Tag target = m_repository.createTag("New", "#00FF00").value();

    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/a.pdf", 0x1111u, source.id()).value())
                    .hasValue());
    ASSERT_TRUE(m_repository.assignTag(FileTagAssociation::create("C:/data/a.pdf", 0x1111u, target.id()).value())
                    .hasValue());

    auto result = m_repository.mergeTags(source.id(), target.id());

    ASSERT_TRUE(result.hasValue());
    auto tags = m_repository.tagsForFile("C:/data/a.pdf");
    ASSERT_TRUE(tags.hasValue());
    ASSERT_EQ(tags.value().size(), 1u);
    EXPECT_EQ(tags.value()[0].id(), target.id());
}

TEST_F(SQLiteTagRepositoryTest, FindFilesWithAnyOfTagGroupsWithEmptyOuterVectorYieldsEmptySuccess)
{
    auto result = m_repository.findFilesWithAnyOfTagGroups({});

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}
