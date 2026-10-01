#include <gtest/gtest.h>

#include <git2.h>

#include <fstream>

#include "LibGit2StatusProvider.h"
#include "PathUtf8.h"

namespace
{
    namespace fs = std::filesystem;

    // Real temp-directory git repos, initialized via libgit2's own C API (git_repository_init) --
    // not a shell-out to the `git` CLI, so this doesn't depend on git being installed on the build
    // machine (Architecture.md §11, same "real adapter, real backing store" posture
    // StandardFileSystemRepositoryTest/ArchiveIndexCacheTest already use).
    class LibGit2StatusProviderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_libgit2_status_provider_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);

            git_libgit2_init();

            git_repository* repo = nullptr;
            ASSERT_EQ(git_repository_init(&repo, PathUtf8::toUtf8(m_tempDir).c_str(), 0), 0);
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

        void stage(const std::string& relativePathUtf8)
        {
            git_index* index = nullptr;
            ASSERT_EQ(git_repository_index(&index, m_repo), 0);
            ASSERT_EQ(git_index_add_bypath(index, relativePathUtf8.c_str()), 0);
            ASSERT_EQ(git_index_write(index), 0);
            git_index_free(index);
        }

        void stageRemoval(const std::string& relativePathUtf8)
        {
            git_index* index = nullptr;
            ASSERT_EQ(git_repository_index(&index, m_repo), 0);
            ASSERT_EQ(git_index_remove_bypath(index, relativePathUtf8.c_str()), 0);
            ASSERT_EQ(git_index_write(index), 0);
            git_index_free(index);
        }

        void commitAll(const std::string& message)
        {
            git_index* index = nullptr;
            ASSERT_EQ(git_repository_index(&index, m_repo), 0);
            ASSERT_EQ(git_index_write(index), 0);

            git_oid treeId;
            ASSERT_EQ(git_index_write_tree(&treeId, index), 0);
            git_index_free(index);

            git_tree* tree = nullptr;
            ASSERT_EQ(git_tree_lookup(&tree, m_repo, &treeId), 0);

            git_signature* signature = nullptr;
            ASSERT_EQ(git_signature_now(&signature, "Test", "test@example.com"), 0);

            git_oid parentOid;
            const bool hasHead = git_reference_name_to_id(&parentOid, m_repo, "HEAD") == 0;
            git_commit* parent = nullptr;
            if (hasHead)
            {
                ASSERT_EQ(git_commit_lookup(&parent, m_repo, &parentOid), 0);
            }

            git_oid commitOid;
            const git_commit* parents[] = { parent };
            ASSERT_EQ(git_commit_create(&commitOid, m_repo, "HEAD", signature, signature, nullptr, message.c_str(), tree,
                                         hasHead ? 1 : 0, hasHead ? parents : nullptr),
                      0);

            if (parent)
            {
                git_commit_free(parent);
            }
            git_tree_free(tree);
            git_signature_free(signature);
        }

        fs::path m_tempDir;
        git_repository* m_repo = nullptr;
    };
}

TEST_F(LibGit2StatusProviderTest, DirectoryWithNoRepositoryReturnsEmptyResult)
{
    const fs::path outsideDir = fs::temp_directory_path() / "explorer_libgit2_status_provider_test_no_repo";
    fs::remove_all(outsideDir);
    fs::create_directories(outsideDir);

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(outsideDir);

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().repositoryRoot.has_value());
    EXPECT_TRUE(result.value().statusByPath.empty());

    fs::remove_all(outsideDir);
}

TEST_F(LibGit2StatusProviderTest, UntrackedFileIsUntracked)
{
    writeFile("a.txt", "hello");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().repositoryRoot.has_value());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "a.txt"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "a.txt"), GitFileStatus::Untracked);
}

TEST_F(LibGit2StatusProviderTest, StagedNewFileIsAddedStaged)
{
    writeFile("a.txt", "hello");
    stage("a.txt");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "a.txt"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "a.txt"), GitFileStatus::AddedStaged);
}

TEST_F(LibGit2StatusProviderTest, CommittedFileModifiedIsModifiedUnstaged)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    writeFile("a.txt", "hello world");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "a.txt"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "a.txt"), GitFileStatus::ModifiedUnstaged);
}

TEST_F(LibGit2StatusProviderTest, CommittedFileModifiedThenStagedIsModifiedStaged)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    writeFile("a.txt", "hello world");
    stage("a.txt");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "a.txt"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "a.txt"), GitFileStatus::ModifiedStaged);
}

TEST_F(LibGit2StatusProviderTest, StagedDeleteIsDeletedStaged)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    fs::remove(m_tempDir / "a.txt");
    stageRemoval("a.txt");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "a.txt"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "a.txt"), GitFileStatus::DeletedStaged);
}

TEST_F(LibGit2StatusProviderTest, UnstagedDeleteIsDeletedUnstaged)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    fs::remove(m_tempDir / "a.txt");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "a.txt"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "a.txt"), GitFileStatus::DeletedUnstaged);
}

TEST_F(LibGit2StatusProviderTest, GitIgnoredFileIsIgnored)
{
    writeFile(".gitignore", "ignored.txt\n");
    stage(".gitignore");
    commitAll("initial");

    writeFile("ignored.txt", "secret");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "ignored.txt"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "ignored.txt"), GitFileStatus::Ignored);
}

TEST_F(LibGit2StatusProviderTest, EntirelyUntrackedSubfolderIsReportedAsItself)
{
    writeFile("sub/inner.txt", "content");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "sub"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "sub"), GitFileStatus::Untracked);
    EXPECT_FALSE(result.value().statusByPath.count(m_tempDir / "sub" / "inner.txt"));
}

TEST_F(LibGit2StatusProviderTest, TrackedSubfolderWithModifiedFileInsideRollsUpModifiedStatusOntoSubfolder)
{
    writeFile("sub/inner.txt", "content");
    stage("sub/inner.txt");
    commitAll("initial");

    writeFile("sub/inner.txt", "changed");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "sub"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "sub"), GitFileStatus::ModifiedUnstaged);
    // Immediate children of the queried directory only -- the deeper, exact path is not itself a
    // key (nothing outside `sub`'s own row would render it).
    EXPECT_FALSE(result.value().statusByPath.count(m_tempDir / "sub" / "inner.txt"));
}

TEST_F(LibGit2StatusProviderTest, ChangeSeveralLevelsDeepRollsUpOntoImmediateChildOnly)
{
    writeFile("sub/inner/deep.txt", "content");
    stage("sub/inner/deep.txt");
    commitAll("initial");

    writeFile("sub/inner/deep.txt", "changed");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "sub"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "sub"), GitFileStatus::ModifiedUnstaged);
    EXPECT_FALSE(result.value().statusByPath.count(m_tempDir / "sub" / "inner"));
}

TEST_F(LibGit2StatusProviderTest, SubfolderContainingOnlyAnIgnoredFileIsNotMarkedIgnored)
{
    writeFile("sub/tracked.txt", "content");
    stage("sub/tracked.txt");
    writeFile(".gitignore", "sub/ignored.txt\n");
    stage(".gitignore");
    commitAll("initial");

    writeFile("sub/ignored.txt", "secret");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    // sub/tracked.txt is unmodified and sub/ignored.txt is only ignored -- "sub" itself has no
    // real change to show, so it must not appear in the rollup at all (and in particular must
    // not be painted Ignored just because the one status entry found underneath it was Ignored).
    EXPECT_FALSE(result.value().statusByPath.count(m_tempDir / "sub"));
}

TEST_F(LibGit2StatusProviderTest, IgnoredPathsReturnsIgnoredFileAndWhollyIgnoredDirectoryUnderRoot)
{
    writeFile(".gitignore", "ignored.txt\nignored_dir/\n");
    stage(".gitignore");
    commitAll("initial");

    writeFile("ignored.txt", "secret");
    writeFile("ignored_dir/inner.txt", "secret2");

    LibGit2StatusProvider provider;
    auto result = provider.ignoredPaths(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    std::vector<fs::path> paths = result.value();
    std::sort(paths.begin(), paths.end());

    EXPECT_EQ(paths, (std::vector<fs::path>{ m_tempDir / "ignored.txt", m_tempDir / "ignored_dir" }));
}

TEST_F(LibGit2StatusProviderTest, IgnoredPathsForDirectoryWithNoRepositoryIsEmptyNotError)
{
    const fs::path outsideDir = fs::temp_directory_path() / "explorer_libgit2_status_provider_test_ignored_no_repo";
    fs::remove_all(outsideDir);
    fs::create_directories(outsideDir);

    LibGit2StatusProvider provider;
    auto result = provider.ignoredPaths(outsideDir);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());

    fs::remove_all(outsideDir);
}

TEST_F(LibGit2StatusProviderTest, RollupPrefersModifiedOverIgnoredWithinSameSubfolder)
{
    writeFile("sub/tracked.txt", "content");
    stage("sub/tracked.txt");
    writeFile(".gitignore", "sub/ignored.txt\n");
    stage(".gitignore");
    commitAll("initial");

    writeFile("sub/tracked.txt", "changed");
    writeFile("sub/ignored.txt", "secret");

    LibGit2StatusProvider provider;
    auto result = provider.statusForDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.value().statusByPath.count(m_tempDir / "sub"));
    EXPECT_EQ(result.value().statusByPath.at(m_tempDir / "sub"), GitFileStatus::ModifiedUnstaged);
}
