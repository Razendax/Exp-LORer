#include <gtest/gtest.h>

#include <fstream>

#include "FileNode.h"
#include "IconLoadingWorker.h"

namespace
{
    namespace fs = std::filesystem;

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    FileNode makeFile(const fs::path& path)
    {
        return FileNode::create(path, 0, std::chrono::system_clock::now(), std::chrono::system_clock::now(), FileType::Regular)
            .value();
    }

    class IconLoadingWorkerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_icon_loading_worker_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        IconLoadingWorker m_worker;
    };
}

// Part of the "opening a 13,000-file folder hangs" fix: requesting only IconSizeMask::Small must
// not resolve the large icon, and a later request for IconSizeMask::Large for the same
// (unchanged) entries must merge into -- not replace -- the already-cached small icon, so a
// view-mode switch never regresses an entry that was already resolved.
TEST_F(IconLoadingWorkerTest, SizeMaskRequestsMergeAcrossCalls)
{
    const fs::path path = m_tempDir / "a.txt";
    writeFile(path, "hello");
    const FileNode entry = makeFile(path);

    QHash<QString, IconImages> smallOnly = m_worker.iconsFor({ entry }, IconSizeMask::Small);
    ASSERT_TRUE(smallOnly.contains(QString::fromStdWString(path.wstring())));
    const IconImages& afterSmall = smallOnly[QString::fromStdWString(path.wstring())];
    EXPECT_FALSE(afterSmall.smallImage.isNull());
    EXPECT_TRUE(afterSmall.largeImage.isNull());

    QHash<QString, IconImages> largeOnly = m_worker.iconsFor({ entry }, IconSizeMask::Large);
    const IconImages& afterLarge = largeOnly[QString::fromStdWString(path.wstring())];
    // The merged record carries both sizes now, not just the one this call asked for.
    EXPECT_FALSE(afterLarge.smallImage.isNull());
    EXPECT_FALSE(afterLarge.largeImage.isNull());
}

// Ordinary files sharing an extension don't need their own SHGetFileInfoW round trip -- this
// doesn't observe the cache key directly (private to IconLoadingWorker), but confirms resolution
// still succeeds correctly for every file once routed through the shared-extension-key path.
TEST_F(IconLoadingWorkerTest, ResolvesIconsForMultipleFilesOfTheSameExtension)
{
    const fs::path pathA = m_tempDir / "a.txt";
    const fs::path pathB = m_tempDir / "b.txt";
    writeFile(pathA, "hello");
    writeFile(pathB, "a different, longer piece of content");

    const std::vector<FileNode> entries{ makeFile(pathA), makeFile(pathB) };
    QHash<QString, IconImages> result = m_worker.iconsFor(entries, IconSizeMask::Both);

    ASSERT_TRUE(result.contains(QString::fromStdWString(pathA.wstring())));
    ASSERT_TRUE(result.contains(QString::fromStdWString(pathB.wstring())));
    EXPECT_FALSE(result[QString::fromStdWString(pathA.wstring())].smallImage.isNull());
    EXPECT_FALSE(result[QString::fromStdWString(pathB.wstring())].smallImage.isNull());
}

TEST_F(IconLoadingWorkerTest, ResolvesIconForADirectory)
{
    fs::create_directory(m_tempDir / "sub");
    const FileNode dirEntry = FileNode::create(m_tempDir / "sub", 0, std::chrono::system_clock::now(),
                                                std::chrono::system_clock::now(), FileType::Directory)
                                   .value();

    QHash<QString, IconImages> result = m_worker.iconsFor({ dirEntry }, IconSizeMask::Both);

    const IconImages& images = result[QString::fromStdWString(dirEntry.path().wstring())];
    EXPECT_FALSE(images.smallImage.isNull());
    EXPECT_FALSE(images.largeImage.isNull());
}
