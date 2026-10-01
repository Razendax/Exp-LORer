#include <gtest/gtest.h>

#include <git2.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include "LibGit2Repository.h"
#include "PathUtf8.h"

namespace
{
    namespace fs = std::filesystem;

    // Real temp-directory git repos, initialized via libgit2's own C API -- same posture as
    // LibGit2StatusProviderTest (Architecture.md §11, no shell-out to the `git` CLI).
    class LibGit2RepositoryTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_libgit2_repository_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);

            git_libgit2_init();

            // Pin the unborn-branch name so the "unborn HEAD" test is deterministic regardless of
            // the build machine's git config (init.defaultBranch).
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

        std::string readFile(const std::string& relativePathUtf8) const
        {
            const fs::path full = m_tempDir / fs::path(std::u8string(relativePathUtf8.begin(), relativePathUtf8.end()));
            std::ifstream stream(full, std::ios::binary);
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }

        void stage(const std::string& relativePathUtf8)
        {
            git_index* index = nullptr;
            ASSERT_EQ(git_repository_index(&index, m_repo), 0);
            ASSERT_EQ(git_index_add_bypath(index, relativePathUtf8.c_str()), 0);
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

        // Creates a local branch pointed at the commit HEAD currently resolves to -- setup
        // infrastructure only, deliberately independent of LibGit2Repository::listLocalBranches
        // (the thing under test).
        void createBranch(const std::string& name)
        {
            git_oid headOid;
            ASSERT_EQ(git_reference_name_to_id(&headOid, m_repo, "HEAD"), 0);
            git_commit* commit = nullptr;
            ASSERT_EQ(git_commit_lookup(&commit, m_repo, &headOid), 0);

            git_reference* branchRef = nullptr;
            ASSERT_EQ(git_branch_create(&branchRef, m_repo, name.c_str(), commit, 0), 0);

            git_reference_free(branchRef);
            git_commit_free(commit);
        }

        // Force-switches HEAD/the working tree to `name` -- setup infrastructure (uses raw git2
        // calls directly, not LibGit2Repository::checkoutBranch, the thing under test) to arrange
        // fixtures where main and a feature branch diverge.
        void checkoutBranchForSetup(const std::string& name)
        {
            git_reference* branchRef = nullptr;
            ASSERT_EQ(git_branch_lookup(&branchRef, m_repo, name.c_str(), GIT_BRANCH_LOCAL), 0);

            git_object* target = nullptr;
            ASSERT_EQ(git_reference_peel(&target, branchRef, GIT_OBJECT_COMMIT), 0);

            git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
            options.checkout_strategy = GIT_CHECKOUT_FORCE;
            ASSERT_EQ(git_checkout_tree(m_repo, target, &options), 0);
            ASSERT_EQ(git_repository_set_head(m_repo, ("refs/heads/" + name).c_str()), 0);

            git_object_free(target);
            git_reference_free(branchRef);
        }

        fs::path m_tempDir;
        git_repository* m_repo = nullptr;
    };
}

TEST_F(LibGit2RepositoryTest, HeadDisplayNameOnNormalBranch)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    bool isDetached = true;
    auto headResult = repository.headDisplayName(isDetached);

    ASSERT_TRUE(headResult.hasValue());
    EXPECT_EQ(headResult.value(), "main");
    EXPECT_FALSE(isDetached);
}

TEST_F(LibGit2RepositoryTest, HeadDisplayNameOnDetachedHeadReturnsShortCommitId)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    git_oid headOid;
    ASSERT_EQ(git_reference_name_to_id(&headOid, m_repo, "HEAD"), 0);
    ASSERT_EQ(git_repository_set_head_detached(m_repo, &headOid), 0);

    char expectedShortId[16] = {};
    git_oid_tostr(expectedShortId, sizeof(expectedShortId), &headOid);

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    bool isDetached = false;
    auto headResult = repository.headDisplayName(isDetached);

    ASSERT_TRUE(headResult.hasValue());
    EXPECT_EQ(headResult.value(), std::string(expectedShortId));
    EXPECT_TRUE(isDetached);
}

TEST_F(LibGit2RepositoryTest, HeadDisplayNameOnUnbornBranchReturnsWouldBeBranchName)
{
    // Zero commits yet -- the repo was init'd with initial_head="main" in SetUp.
    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    bool isDetached = true;
    auto headResult = repository.headDisplayName(isDetached);

    ASSERT_TRUE(headResult.hasValue());
    EXPECT_EQ(headResult.value(), "main");
    EXPECT_FALSE(isDetached);
}

TEST_F(LibGit2RepositoryTest, ChangedFileCountReflectsStagedUnstagedAndUntrackedButExcludesIgnored)
{
    writeFile("committed.txt", "hello");
    stage("committed.txt");
    writeFile(".gitignore", "ignored.txt\n");
    stage(".gitignore");
    commitAll("initial");

    writeFile("committed.txt", "changed"); // unstaged modification
    writeFile("new_staged.txt", "content");
    stage("new_staged.txt"); // staged addition
    writeFile("untracked.txt", "content"); // untracked
    writeFile("ignored.txt", "secret"); // ignored -- must not count

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    auto countResult = repository.changedFileCount();

    ASSERT_TRUE(countResult.hasValue());
    EXPECT_EQ(countResult.value(), 3);
}

TEST_F(LibGit2RepositoryTest, IgnoredPathsReturnsIgnoredFileAndWhollyIgnoredDirectoryAsSingleEntries)
{
    writeFile(".gitignore", "ignored.txt\nignored_dir/\n");
    stage(".gitignore");
    commitAll("initial");

    writeFile("ignored.txt", "secret");
    writeFile("ignored_dir/inner.txt", "secret2");
    writeFile("tracked.txt", "kept");
    stage("tracked.txt");
    commitAll("tracked");

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    auto ignoredResult = repository.ignoredPaths("");

    ASSERT_TRUE(ignoredResult.hasValue());
    std::vector<std::string> paths = ignoredResult.value();
    std::sort(paths.begin(), paths.end());

    // ignored_dir is reported as a single entry (trailing slash), not walked file-by-file --
    // RECURSE_IGNORED_DIRS left off mirrors statusForImmediateChildren's own untracked-folder
    // behavior.
    EXPECT_EQ(paths, (std::vector<std::string>{ "ignored.txt", "ignored_dir/" }));
}

TEST_F(LibGit2RepositoryTest, IgnoredPathsIsEmptyWhenNothingIsIgnored)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    auto ignoredResult = repository.ignoredPaths("");

    ASSERT_TRUE(ignoredResult.hasValue());
    EXPECT_TRUE(ignoredResult.value().empty());
}

TEST_F(LibGit2RepositoryTest, ListLocalBranchesReturnsExactlyCreatedBranchesNoRemotes)
{
    writeFile("a.txt", "hello");
    stage("a.txt");
    commitAll("initial");

    createBranch("feature");
    createBranch("bugfix");

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    auto branchesResult = repository.listLocalBranches();

    ASSERT_TRUE(branchesResult.hasValue());
    std::vector<std::string> branches = branchesResult.value();
    std::sort(branches.begin(), branches.end());
    EXPECT_EQ(branches, (std::vector<std::string>{ "bugfix", "feature", "main" }));
}

TEST_F(LibGit2RepositoryTest, CheckoutOnCleanWorkingTreeSwitchesHeadAndUpdatesTrackedFiles)
{
    writeFile("a.txt", "main content");
    stage("a.txt");
    commitAll("initial");

    createBranch("feature");
    checkoutBranchForSetup("feature");
    writeFile("a.txt", "feature content");
    stage("a.txt");
    commitAll("feature commit");

    checkoutBranchForSetup("main");
    ASSERT_EQ(readFile("a.txt"), "main content");

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    auto checkoutResult = repository.checkoutBranch("feature");
    ASSERT_TRUE(checkoutResult.hasValue());

    EXPECT_EQ(readFile("a.txt"), "feature content");

    bool isDetached = true;
    auto headResult = repository.headDisplayName(isDetached);
    ASSERT_TRUE(headResult.hasValue());
    EXPECT_EQ(headResult.value(), "feature");
    EXPECT_FALSE(isDetached);
}

TEST_F(LibGit2RepositoryTest, CheckoutWithConflictingUncommittedChangeFailsAndLeavesStateUntouched)
{
    writeFile("a.txt", "main content");
    stage("a.txt");
    commitAll("initial");

    createBranch("feature");
    checkoutBranchForSetup("feature");
    writeFile("a.txt", "feature content");
    stage("a.txt");
    commitAll("feature commit");

    checkoutBranchForSetup("main");
    writeFile("a.txt", "uncommitted local edit"); // unstaged -- conflicts with feature's own edit

    auto repoResult = LibGit2Repository::open(m_tempDir);
    ASSERT_TRUE(repoResult.hasValue());
    LibGit2Repository repository = std::move(repoResult).value();

    auto checkoutResult = repository.checkoutBranch("feature");
    EXPECT_TRUE(checkoutResult.hasError());

    // Nothing on disk changed, and HEAD is still main.
    EXPECT_EQ(readFile("a.txt"), "uncommitted local edit");

    bool isDetached = true;
    auto headResult = repository.headDisplayName(isDetached);
    ASSERT_TRUE(headResult.hasValue());
    EXPECT_EQ(headResult.value(), "main");
    EXPECT_FALSE(isDetached);
}
