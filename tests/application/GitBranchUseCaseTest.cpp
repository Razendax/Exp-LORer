#include <gtest/gtest.h>

#include "GitBranchUseCase.h"
#include "MockGitBranchProvider.h"

using ::testing::Return;

TEST(GitBranchUseCase, RepositorySummaryDelegatesToProviderAndPassesThroughSuccess)
{
    MockGitBranchProvider provider;

    GitRepositorySummary summary;
    summary.repositoryRoot = std::filesystem::path("C:/repo");
    summary.headDisplayName = "main";
    summary.isDetached = false;
    summary.changedFileCount = 3;

    EXPECT_CALL(provider, repositorySummary(std::filesystem::path("C:/repo/sub")))
        .WillOnce(Return(Result<GitRepositorySummary>::success(summary)));

    GitBranchUseCase useCase(provider);
    auto result = useCase.repositorySummary(std::filesystem::path("C:/repo/sub"));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().repositoryRoot, std::filesystem::path("C:/repo"));
    EXPECT_EQ(result.value().headDisplayName, "main");
    EXPECT_FALSE(result.value().isDetached);
    EXPECT_EQ(result.value().changedFileCount, 3);
}

TEST(GitBranchUseCase, RepositorySummaryDirectoryNotInAnyRepositoryIsToleratedAsEmptySuccess)
{
    MockGitBranchProvider provider;

    EXPECT_CALL(provider, repositorySummary(::testing::_))
        .WillOnce(Return(Result<GitRepositorySummary>::success(GitRepositorySummary{})));

    GitBranchUseCase useCase(provider);
    auto result = useCase.repositorySummary(std::filesystem::path("C:/not-a-repo"));

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().repositoryRoot.has_value());
}

TEST(GitBranchUseCase, RepositorySummaryPassesThroughFailure)
{
    MockGitBranchProvider provider;

    EXPECT_CALL(provider, repositorySummary(::testing::_))
        .WillOnce(Return(Result<GitRepositorySummary>::failure(Error(ErrorCode::IoError, "corrupt repo"))));

    GitBranchUseCase useCase(provider);
    auto result = useCase.repositorySummary(std::filesystem::path("C:/repo"));

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(GitBranchUseCase, ListLocalBranchesDelegatesToProvider)
{
    MockGitBranchProvider provider;

    std::vector<std::string> branches{ "main", "feature/x" };
    EXPECT_CALL(provider, listLocalBranches(std::filesystem::path("C:/repo")))
        .WillOnce(Return(Result<std::vector<std::string>>::success(branches)));

    GitBranchUseCase useCase(provider);
    auto result = useCase.listLocalBranches(std::filesystem::path("C:/repo"));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), branches);
}

TEST(GitBranchUseCase, ListLocalBranchesPassesThroughFailure)
{
    MockGitBranchProvider provider;

    EXPECT_CALL(provider, listLocalBranches(::testing::_))
        .WillOnce(Return(Result<std::vector<std::string>>::failure(Error(ErrorCode::NotFound, "not a repo"))));

    GitBranchUseCase useCase(provider);
    auto result = useCase.listLocalBranches(std::filesystem::path("C:/not-a-repo"));

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(GitBranchUseCase, CheckoutBranchDelegatesToProviderAndPassesThroughSuccess)
{
    MockGitBranchProvider provider;

    EXPECT_CALL(provider, checkoutBranch(std::filesystem::path("C:/repo"), std::string("feature/x")))
        .WillOnce(Return(Result<void>::success()));

    GitBranchUseCase useCase(provider);
    auto result = useCase.checkoutBranch(std::filesystem::path("C:/repo"), "feature/x");

    EXPECT_TRUE(result.hasValue());
}

TEST(GitBranchUseCase, CheckoutBranchPassesThroughFailure)
{
    MockGitBranchProvider provider;

    EXPECT_CALL(provider, checkoutBranch(::testing::_, ::testing::_))
        .WillOnce(Return(Result<void>::failure(Error(ErrorCode::IoError, "would overwrite local changes"))));

    GitBranchUseCase useCase(provider);
    auto result = useCase.checkoutBranch(std::filesystem::path("C:/repo"), "feature/x");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}
