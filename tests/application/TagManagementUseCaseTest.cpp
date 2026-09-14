#include <gtest/gtest.h>

#include "MockFileSystemRepository.h"
#include "MockTagRepository.h"
#include "TagManagementUseCase.h"

using ::testing::Return;
using ::testing::_;

namespace
{
    FileNode makeFile(const std::filesystem::path& path, std::optional<std::uint64_t> hash = std::nullopt)
    {
        auto file = FileNode::create(path, 10, std::chrono::system_clock::now(), std::chrono::system_clock::now(),
                                      FileType::Regular)
                        .value();
        return hash.has_value() ? file.withHash(hash.value()) : file;
    }
}

TEST(TagManagementUseCase, CreateTagRejectsInvalidColorBeforeHittingRepository)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, createTag(_, _)).Times(0);

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.createTag("Work", "not-a-color");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(TagManagementUseCase, CreateTagDelegatesToRepositoryWhenValid)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, createTag("Work", "#FF8800"))
        .WillOnce(Return(Result<Tag>::success(Tag::create(1, "Work", "#FF8800").value())));

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.createTag("Work", "#FF8800");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().id(), 1);
}

TEST(TagManagementUseCase, UpdateTagRejectsUnassignedId)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, updateTag(_)).Times(0);

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.updateTag(Tag::create(Tag::kUnassignedId, "Work", "#FF8800").value());

    EXPECT_TRUE(result.hasError());
}

TEST(TagManagementUseCase, DeleteTagRejectsUnassignedId)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, deleteTag(_)).Times(0);

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.deleteTag(Tag::kUnassignedId);

    EXPECT_TRUE(result.hasError());
}

TEST(TagManagementUseCase, AssignTagFailsWhenAlreadyAssigned)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    Tag existing = Tag::create(1, "Work", "#FF8800").value();
    EXPECT_CALL(tagRepository, tagsForFile(_)).WillOnce(Return(Result<std::vector<Tag>>::success({ existing })));
    EXPECT_CALL(tagRepository, assignTag(_)).Times(0);
    EXPECT_CALL(fileSystemRepository, computeFileHash(_)).Times(0);

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.assignTag(makeFile("C:/data/report.pdf"), 1);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::AlreadyExists);
}

TEST(TagManagementUseCase, AssignTagComputesHashWhenFileHasNone)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, tagsForFile(_)).WillOnce(Return(Result<std::vector<Tag>>::success({})));
    EXPECT_CALL(fileSystemRepository, computeFileHash(_)).WillOnce(Return(Result<std::uint64_t>::success(0xABCDu)));
    EXPECT_CALL(tagRepository, assignTag(_))
        .WillOnce([](const FileTagAssociation& association) {
            EXPECT_TRUE(association.fileHash().has_value());
            EXPECT_EQ(association.fileHash().value(), 0xABCDu);
            EXPECT_EQ(association.tagId(), 1);
            return Result<void>::success();
        });

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.assignTag(makeFile("C:/data/report.pdf"), 1);

    EXPECT_TRUE(result.hasValue());
}

TEST(TagManagementUseCase, AssignTagSkipsHashComputationWhenFileAlreadyHasOne)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, tagsForFile(_)).WillOnce(Return(Result<std::vector<Tag>>::success({})));
    EXPECT_CALL(fileSystemRepository, computeFileHash(_)).Times(0);
    EXPECT_CALL(tagRepository, assignTag(_)).WillOnce(Return(Result<void>::success()));

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.assignTag(makeFile("C:/data/report.pdf", 0x1111u), 1);

    EXPECT_TRUE(result.hasValue());
}

TEST(TagManagementUseCase, AssignTagPropagatesHashComputationFailure)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, tagsForFile(_)).WillOnce(Return(Result<std::vector<Tag>>::success({})));
    EXPECT_CALL(fileSystemRepository, computeFileHash(_))
        .WillOnce(Return(Result<std::uint64_t>::failure(Error(ErrorCode::IoError, "unreadable"))));
    EXPECT_CALL(tagRepository, assignTag(_)).Times(0);

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.assignTag(makeFile("C:/data/report.pdf"), 1);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(TagManagementUseCase, UnassignTagDelegatesToRepository)
{
    MockTagRepository tagRepository;
    MockFileSystemRepository fileSystemRepository;
    EXPECT_CALL(tagRepository, unassignTag(std::filesystem::path("C:/data/report.pdf"), 1))
        .WillOnce(Return(Result<void>::success()));

    TagManagementUseCase useCase(tagRepository, fileSystemRepository);
    auto result = useCase.unassignTag(makeFile("C:/data/report.pdf"), 1);

    EXPECT_TRUE(result.hasValue());
}
