#include <gtest/gtest.h>

#include "GitStatusUseCase.h"
#include "MockGitStatusProvider.h"

using ::testing::Return;

TEST(GitStatusUseCase, DelegatesToProviderAndPassesThroughSuccess)
{
    MockGitStatusProvider provider;

    GitDirectoryStatus status;
    status.repositoryRoot = std::filesystem::path("C:/repo");
    status.statusByPath[std::filesystem::path("C:/repo/a.txt")] = GitFileStatus::ModifiedUnstaged;

    EXPECT_CALL(provider, statusForDirectory(std::filesystem::path("C:/repo/sub")))
        .WillOnce(Return(Result<GitDirectoryStatus>::success(status)));

    GitStatusUseCase useCase(provider);
    auto result = useCase.statusForDirectory(std::filesystem::path("C:/repo/sub"));

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().repositoryRoot, std::filesystem::path("C:/repo"));
    ASSERT_EQ(result.value().statusByPath.size(), 1u);
    EXPECT_EQ(result.value().statusByPath.at(std::filesystem::path("C:/repo/a.txt")), GitFileStatus::ModifiedUnstaged);
}

TEST(GitStatusUseCase, DelegatesToProviderAndPassesThroughFailure)
{
    MockGitStatusProvider provider;

    EXPECT_CALL(provider, statusForDirectory(::testing::_))
        .WillOnce(Return(Result<GitDirectoryStatus>::failure(Error(ErrorCode::IoError, "corrupt repo"))));

    GitStatusUseCase useCase(provider);
    auto result = useCase.statusForDirectory(std::filesystem::path("C:/repo"));

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(GitStatusUseCase, DirectoryNotInAnyRepositoryIsToleratedAsEmptySuccess)
{
    MockGitStatusProvider provider;

    EXPECT_CALL(provider, statusForDirectory(::testing::_)).WillOnce(Return(Result<GitDirectoryStatus>::success(GitDirectoryStatus{})));

    GitStatusUseCase useCase(provider);
    auto result = useCase.statusForDirectory(std::filesystem::path("C:/not-a-repo"));

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().repositoryRoot.has_value());
    EXPECT_TRUE(result.value().statusByPath.empty());
}
