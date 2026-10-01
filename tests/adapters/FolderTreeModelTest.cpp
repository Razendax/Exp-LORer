#include <gtest/gtest.h>

#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

#include "FileNavigationUseCase.h"
#include "FolderTreeModel.h"
#include "PathContainment.h"
#include "ShellContextMenuProvider.h"
#include "StandardFileSystemRepository.h"
#include "VirtualPaths.h"

namespace
{
    namespace fs = std::filesystem;

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    QString toQString(const fs::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // Real temp directories + StandardFileSystemRepository, per Architecture.md §11's adapter-test
    // posture. The tree is always rooted at "This PC", so every test reaches its fixture folder
    // through indexForPath() from the fixture's drive down.
    class FolderTreeModelTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // Canonical so every component matches the on-disk name the tree lists (no 8.3 short
            // names from temp_directory_path()).
            m_tempDir = fs::weakly_canonical(fs::temp_directory_path()) / "explorer_folder_tree_model_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);

            // Deterministic regardless of the machine's QSettings. The temp dir usually sits under
            // a hidden folder (AppData); revealing it works anyway (see
            // RevealShowsHiddenAncestorsButKeepsOtherHiddenFoldersFiltered).
            m_model.setShowHiddenFiles(false);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        QModelIndex fetchedIndexFor(const fs::path& path)
        {
            const QModelIndex index = m_model.indexForPath(path);
            if (m_model.canFetchMore(index))
            {
                m_model.fetchMore(index);
            }
            return index;
        }

        QStringList childNames(const QModelIndex& parent) const
        {
            QStringList names;
            for (int row = 0; row < m_model.rowCount(parent); ++row)
            {
                names.append(m_model.index(row, 0, parent).data(Qt::DisplayRole).toString());
            }
            return names;
        }

        fs::path m_tempDir;
        StandardFileSystemRepository m_fileSystemRepository;
        ShellContextMenuProvider m_contextMenuProvider;
        FileNavigationUseCase m_fileNavigationUseCase{ m_fileSystemRepository, m_contextMenuProvider };
        FolderTreeModel m_model{ m_fileNavigationUseCase };
    };
}

TEST_F(FolderTreeModelTest, RootHoldsOnlyThisPcWhichIsNotEditable)
{
    ASSERT_EQ(m_model.rowCount(), 1);
    const QModelIndex thisPc = m_model.index(0, 0);
    EXPECT_EQ(m_model.pathForIndex(thisPc), VirtualPaths::ThisPC);
    EXPECT_EQ(m_model.indexForPath(VirtualPaths::ThisPC), thisPc);
    EXPECT_TRUE(m_model.isTopLevelLocation(thisPc));
    EXPECT_FALSE(m_model.flags(thisPc).testFlag(Qt::ItemIsEditable));
}

TEST_F(FolderTreeModelTest, ListsOnlyDirectoriesSortedCaseInsensitively)
{
    fs::create_directory(m_tempDir / "beta");
    fs::create_directory(m_tempDir / "Alpha");
    fs::create_directory(m_tempDir / "gamma");
    writeFile(m_tempDir / "Aaa.txt", "x");
    writeFile(m_tempDir / "zeta.txt", "x");

    const QModelIndex fixture = fetchedIndexFor(m_tempDir);

    ASSERT_TRUE(fixture.isValid());
    EXPECT_EQ(childNames(fixture), (QStringList{ "Alpha", "beta", "gamma" }));
    EXPECT_TRUE(m_model.flags(m_model.index(0, 0, fixture)).testFlag(Qt::ItemIsEditable));
    EXPECT_FALSE(m_model.isTopLevelLocation(m_model.index(0, 0, fixture)));
}

TEST_F(FolderTreeModelTest, ChildrenAreLoadedLazilyAndALeafLosesItsArrow)
{
    fs::create_directory(m_tempDir / "leaf");

    const QModelIndex leaf = m_model.indexForPath(m_tempDir / "leaf");
    ASSERT_TRUE(leaf.isValid());

    // Not listed yet: optimistic arrow, nothing loaded.
    EXPECT_TRUE(m_model.hasChildren(leaf));
    EXPECT_TRUE(m_model.canFetchMore(leaf));
    EXPECT_EQ(m_model.rowCount(leaf), 0);

    m_model.fetchMore(leaf);

    EXPECT_FALSE(m_model.canFetchMore(leaf));
    EXPECT_FALSE(m_model.hasChildren(leaf));
}

TEST_F(FolderTreeModelTest, IndexForDeepPathLoadsEachLevelAndRoundTrips)
{
    const fs::path deep = m_tempDir / "a" / "b" / "c";
    fs::create_directories(deep);

    const QModelIndex index = m_model.indexForPath(deep);

    ASSERT_TRUE(index.isValid());
    EXPECT_TRUE(pathsEqualCaseInsensitive(m_model.pathForIndex(index), deep));
    EXPECT_EQ(index.data(Qt::DisplayRole).toString(), "c");
    EXPECT_EQ(index.parent().data(Qt::DisplayRole).toString(), "b");
    EXPECT_EQ(index.parent().parent().data(Qt::DisplayRole).toString(), "a");
    EXPECT_EQ(index.data(FolderTreeModel::PathRole).toString(), toQString(m_model.pathForIndex(index)));
}

TEST_F(FolderTreeModelTest, IndexForPathMatchesComponentsCaseInsensitively)
{
    fs::create_directories(m_tempDir / "MixedCase" / "Inner");

    const QModelIndex index = m_model.indexForPath(m_tempDir / "mixedcase" / "INNER");

    ASSERT_TRUE(index.isValid());
    EXPECT_EQ(index.data(Qt::DisplayRole).toString(), "Inner");
}

TEST_F(FolderTreeModelTest, NonAsciiFolderNamesListDisplayAndRoundTrip)
{
    const fs::path deep = m_tempDir / fs::path(u8"Папка") / fs::path(u8"日本語") / fs::path(u8"😀 emoji");
    fs::create_directories(deep);

    const QModelIndex index = m_model.indexForPath(deep);

    ASSERT_TRUE(index.isValid());
    EXPECT_TRUE(pathsEqualCaseInsensitive(m_model.pathForIndex(index), deep));
    EXPECT_EQ(index.data(Qt::DisplayRole).toString(), QString::fromUtf8(u8"😀 emoji"));
    EXPECT_EQ(index.parent().data(Qt::DisplayRole).toString(), QString::fromUtf8(u8"日本語"));
    EXPECT_EQ(index.parent().parent().data(Qt::DisplayRole).toString(), QString::fromUtf8(u8"Папка"));
}

TEST_F(FolderTreeModelTest, PathInsideArchiveRevealsTheArchivesContainingFolder)
{
    const QModelIndex index = m_model.indexForPath(m_tempDir / "bundle.zip" / "inner");

    ASSERT_TRUE(index.isValid());
    EXPECT_TRUE(pathsEqualCaseInsensitive(m_model.pathForIndex(index), m_tempDir));
}

TEST_F(FolderTreeModelTest, IndexForMissingPathStopsAtDeepestExistingFolder)
{
    fs::create_directory(m_tempDir / "exists");

    const QModelIndex index = m_model.indexForPath(m_tempDir / "exists" / "missing" / "deeper");

    ASSERT_TRUE(index.isValid());
    EXPECT_TRUE(pathsEqualCaseInsensitive(m_model.pathForIndex(index), m_tempDir / "exists"));
}

TEST_F(FolderTreeModelTest, RefreshDirectoryDiffsRowsAndPreservesLoadedSiblingSubtree)
{
    fs::create_directories(m_tempDir / "keep" / "inner");
    fs::create_directory(m_tempDir / "gone");

    const QModelIndex fixture = fetchedIndexFor(m_tempDir);
    fetchedIndexFor(m_tempDir / "keep");
    ASSERT_EQ(childNames(fixture), (QStringList{ "gone", "keep" }));

    fs::remove_all(m_tempDir / "gone");
    fs::create_directory(m_tempDir / "new");
    m_model.refreshDirectory(m_tempDir);

    EXPECT_EQ(childNames(fixture), (QStringList{ "keep", "new" }));
    const QModelIndex keep = m_model.index(0, 0, fixture);
    EXPECT_FALSE(m_model.canFetchMore(keep));
    EXPECT_EQ(childNames(keep), (QStringList{ "inner" }));
    EXPECT_TRUE(m_model.canFetchMore(m_model.index(1, 0, fixture)));
}

TEST_F(FolderTreeModelTest, RefreshDirectoryIsNoOpForUnlistedNode)
{
    fs::create_directory(m_tempDir / "unlisted");
    const QModelIndex unlisted = m_model.indexForPath(m_tempDir / "unlisted");
    fs::create_directory(m_tempDir / "unlisted" / "child");

    m_model.refreshDirectory(m_tempDir / "unlisted");

    EXPECT_TRUE(m_model.canFetchMore(unlisted));
    EXPECT_EQ(m_model.rowCount(unlisted), 0);
}

TEST_F(FolderTreeModelTest, ApplyListingDiffsWithoutTouchingDiskAndIgnoresFiles)
{
    fs::create_directory(m_tempDir / "onDisk");
    const QModelIndex fixture = fetchedIndexFor(m_tempDir);
    ASSERT_EQ(childNames(fixture), (QStringList{ "onDisk" }));

    const auto now = std::chrono::system_clock::now();
    std::vector<FileNode> entries;
    entries.push_back(FileNode::create(m_tempDir / "onlyInListing", 0, now, now, FileType::Directory).value());
    entries.push_back(FileNode::create(m_tempDir / "file.txt", 3, now, now, FileType::Regular).value());

    m_model.applyListing(m_tempDir, entries);

    EXPECT_EQ(childNames(fixture), (QStringList{ "onlyInListing" }));
}

TEST_F(FolderTreeModelTest, RenameNodeRewritesDescendantsAndKeepsSubtree)
{
    fs::create_directories(m_tempDir / "old" / "child" / "grand");
    fetchedIndexFor(m_tempDir / "old" / "child");

    fs::rename(m_tempDir / "old", m_tempDir / "renamed");
    m_model.renameNode(m_tempDir / "old", m_tempDir / "renamed");

    const QModelIndex fixture = m_model.indexForPath(m_tempDir);
    ASSERT_EQ(childNames(fixture), (QStringList{ "renamed" }));
    const QModelIndex child = m_model.index(0, 0, m_model.index(0, 0, fixture));
    EXPECT_TRUE(pathsEqualCaseInsensitive(m_model.pathForIndex(child), m_tempDir / "renamed" / "child"));
    EXPECT_FALSE(m_model.canFetchMore(child)); // subtree survived rather than being re-created

    const QModelIndex grand = m_model.indexForPath(m_tempDir / "renamed" / "child" / "grand");
    ASSERT_TRUE(grand.isValid());
    EXPECT_EQ(grand.parent(), child);
}

TEST_F(FolderTreeModelTest, RenameNodeResortsAmongSiblings)
{
    fs::create_directory(m_tempDir / "a");
    fs::create_directory(m_tempDir / "m");
    const QModelIndex fixture = fetchedIndexFor(m_tempDir);

    fs::rename(m_tempDir / "a", m_tempDir / "z");
    m_model.renameNode(m_tempDir / "a", m_tempDir / "z");

    EXPECT_EQ(childNames(fixture), (QStringList{ "m", "z" }));
}

#ifdef _WIN32
TEST_F(FolderTreeModelTest, RevealShowsHiddenAncestorsButKeepsOtherHiddenFoldersFiltered)
{
    fs::create_directories(m_tempDir / "hiddenParent" / "child");
    fs::create_directory(m_tempDir / "otherHidden");
    fs::create_directory(m_tempDir / "plain");
    ASSERT_TRUE(SetFileAttributesW((m_tempDir / "hiddenParent").c_str(), FILE_ATTRIBUTE_HIDDEN));
    ASSERT_TRUE(SetFileAttributesW((m_tempDir / "otherHidden").c_str(), FILE_ATTRIBUTE_HIDDEN));

    // Listed first, before the hidden folder is on a revealed path.
    const QModelIndex fixture = fetchedIndexFor(m_tempDir);
    ASSERT_EQ(childNames(fixture), (QStringList{ "plain" }));

    const QModelIndex child = m_model.indexForPath(m_tempDir / "hiddenParent" / "child");

    ASSERT_TRUE(child.isValid());
    EXPECT_TRUE(pathsEqualCaseInsensitive(m_model.pathForIndex(child), m_tempDir / "hiddenParent" / "child"));
    EXPECT_EQ(childNames(fixture), (QStringList{ "hiddenParent", "plain" }));

    SetFileAttributesW((m_tempDir / "hiddenParent").c_str(), FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW((m_tempDir / "otherHidden").c_str(), FILE_ATTRIBUTE_NORMAL);
}

TEST_F(FolderTreeModelTest, HiddenFoldersFollowShowHiddenToggle)
{
    // Its own fixture under the working directory rather than m_tempDir, so the toggle's re-list
    // of every loaded level only ever touches ordinary folders.
    const fs::path root = fs::weakly_canonical(fs::current_path()) / "explorer_folder_tree_hidden_test";
    fs::remove_all(root);
    fs::create_directories(root / "visible");
    fs::create_directory(root / "secret");
    ASSERT_TRUE(SetFileAttributesW((root / "secret").c_str(), FILE_ATTRIBUTE_HIDDEN));

    const QModelIndex fixture = fetchedIndexFor(root);
    ASSERT_TRUE(fixture.isValid());
    EXPECT_EQ(childNames(fixture), (QStringList{ "visible" }));

    m_model.setShowHiddenFiles(true);
    EXPECT_EQ(childNames(fixture), (QStringList{ "secret", "visible" }));

    m_model.setShowHiddenFiles(false);
    EXPECT_EQ(childNames(fixture), (QStringList{ "visible" }));

    SetFileAttributesW((root / "secret").c_str(), FILE_ATTRIBUTE_NORMAL);
    fs::remove_all(root);
}
#endif
