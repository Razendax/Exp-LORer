#include <gtest/gtest.h>

#include <git2.h>

#include <fstream>

#include "LibGit2BranchProvider.h"
#include "PathUtf8.h"

namespace
{
    namespace fs = std::filesystem;

    // Thin wrapper-level tests -- LibGit2RepositoryTest already covers headDisplayName/
    // changedFileCount/listLocalBranches/checkoutBranch's own logic in depth; this only checks
    // LibGit2BranchProvider's directory-to-repository plumbing and its Port-contract edge cases
    // (mirrors LibGit2StatusProviderTest's split of responsibility).
    class LibGit2BranchProviderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_libgit2_branch_provider_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);

            git_libgit2_init();

            git_repository_init_options initOptions = GIT_REPOSITORY_INIT_OPTIONS_INIT;
            initOptions.initial_head = "main";

            git_repository* repo = nullptr;
            ASSERT_EQ(git_repository_init_ext(&repo, PathUtf8::toUtf8(m_tempDir).c_str(), &initOptions), 0);
            m_repo = repo;
        }

        void TearDown() override
        {
            if (m_repo)
            {
                git_repository_free(m_repo);
                m_repo = nullptr;
            }
            git_libgit2_shutdown();
            fs::remove_all(m_tempDir);
        }

        void writeFile(const std::string& relativePathUtf8, const std::string& content)
        {
            const fs::path full = m_tempDir / fs::path(std::u8string(relativePathUtf8.begin(), relativePathUtf8.end()));
            fs::create_directories(full.parent_path());
            std::ofstream stream(full, std::ios::binary);
            stream << content;
        }

        void stageAndCommit(const std::string& relativePathUtf8, const std::string& message)
        {
            git_index* index = nullptr;
            ASSERT_EQ(git_repository_index(&index, m_repo), 0);
            ASSERT_EQ(git_index_add_bypath(index, relativePathUtf8.c_str()), 0);
            ASSERT_EQ(git_index_write(index), 0);

            git_oid treeId;
            ASSERT_EQ(git_index_write_tree(&treeId, index), 0);
            git_index_free(index);

            git_tree* tree = nullptr;
            ASSERT_EQ(git_tree_lookup(&tree, m_repo, &treeId), 0);

            git_signature* signature = nullptr;
            ASSERT_EQ(git_signature_now(&signature, "Test", "test@example.com"), 0);

            git_oid commitOid;
            ASSERT_EQ(git_commit_create(&commitOid, m_repo, "HEAD", signature, signature, nullptr, message.c_str(), tree, 0, nullptr), 0);

            git_tree_free(tree);
            git_signature_free(signature);
        }

        fs::path m_tempDir;
        git_repository* m_repo = nullptr;
    };
}

TEST_F(LibGit2BranchProviderTest, DirectoryWithNoRepositoryReturnsBlankedSummary)
{
    const fs::path outsideDir = fs::temp_directory_path() / "explorer_libgit2_branch_provider_test_no_repo";
    fs::remove_all(outsideDir);
    fs::create_directories(outsideDir);

    LibGit2BranchProvider provider;
    auto result = provider.repositorySummary(outsideDir);

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().repositoryRoot.has_value());
    EXPECT_EQ(result.value().headDisplayName, std::string());
    EXPECT_FALSE(result.value().isDetached);
    EXPECT_EQ(result.value().changedFileCount, 0);

    fs::remove_all(outsideDir);
}

TEST_F(LibGit2BranchProviderTest, DirectoryInsideRepositoryReturnsRealSummary)
{
    writeFile("a.txt", "hello");
    stageAndCommit("a.txt", "initial");
    writeFile("b.txt", "untracked");

    LibGit2BranchProvider provider;
    auto result = provider.repositorySummary(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().repositoryRoot.has_value());
    // libgit2's git_repository_workdir() always returns a trailing-slash path -- compare by
    // filesystem identity rather than lexical equality (same reasoning applies to
    // LibGit2StatusProvider, which never compares repositoryRoot against the queried directory
    // directly for this exact reason).
    EXPECT_TRUE(fs::equivalent(*result.value().repositoryRoot, m_tempDir));
    EXPECT_EQ(result.value().headDisplayName, "main");
    EXPECT_FALSE(result.value().isDetached);
    EXPECT_EQ(result.value().changedFileCount, 1);
}

TEST_F(LibGit2BranchProviderTest, ListLocalBranchesOnDirectoryNotARepositoryFails)
{
    const fs::path outsideDir = fs::temp_directory_path() / "explorer_libgit2_branch_provider_test_no_repo_branches";
    fs::remove_all(outsideDir);
    fs::create_directories(outsideDir);

    LibGit2BranchProvider provider;
    auto result = provider.listLocalBranches(outsideDir);

    EXPECT_TRUE(result.hasError());

    fs::remove_all(outsideDir);
}

TEST_F(LibGit2BranchProviderTest, CheckoutBranchOnDirectoryNotARepositoryFails)
{
    const fs::path outsideDir = fs::temp_directory_path() / "explorer_libgit2_branch_provider_test_no_repo_checkout";
    fs::remove_all(outsideDir);
    fs::create_directories(outsideDir);

    LibGit2BranchProvider provider;
    auto result = provider.checkoutBranch(outsideDir, "main");

    EXPECT_TRUE(result.hasError());

    fs::remove_all(outsideDir);
}

TEST_F(LibGit2BranchProviderTest, ListLocalBranchesInsideRepositoryReturnsRepositoryBranches)
{
    writeFile("a.txt", "hello");
    stageAndCommit("a.txt", "initial");

    LibGit2BranchProvider provider;
    auto result = provider.listLocalBranches(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), (std::vector<std::string>{ "main" }));
}
