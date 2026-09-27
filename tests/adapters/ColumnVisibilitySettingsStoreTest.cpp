#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ColumnVisibilitySettingsStore.h"

namespace
{
    namespace fs = std::filesystem;

    class ColumnVisibilitySettingsStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_column_visibility_settings_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_settingsFilePath = m_tempDir / "column_visibility.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_settingsFilePath;
    };
}

TEST_F(ColumnVisibilitySettingsStoreTest, LoadOnMissingFileReturnsAllVisibleDefaults)
{
    ColumnVisibilitySettingsStore store(m_settingsFilePath);

    const ColumnVisibilitySettings settings = store.load();

    for (int column = FileListModel::NameColumn + 1; column < FileListModel::ColumnCount; ++column)
    {
        EXPECT_TRUE(settings.visible[static_cast<size_t>(column)]);
    }
}

TEST_F(ColumnVisibilitySettingsStoreTest, SaveThenLoadRoundTripsEveryColumn)
{
    ColumnVisibilitySettingsStore store(m_settingsFilePath);

    ColumnVisibilitySettings settings;
    settings.visible[FileListModel::SizeColumn] = false;
    settings.visible[FileListModel::TypeColumn] = true;
    settings.visible[FileListModel::DateModifiedColumn] = false;
    settings.visible[FileListModel::GitStatusColumn] = true;
    settings.visible[FileListModel::TagsColumn] = false;
    settings.visible[FileListModel::DateCreatedColumn] = true;

    ASSERT_TRUE(store.save(settings));

    const ColumnVisibilitySettings loaded = store.load();

    EXPECT_FALSE(loaded.visible[FileListModel::SizeColumn]);
    EXPECT_TRUE(loaded.visible[FileListModel::TypeColumn]);
    EXPECT_FALSE(loaded.visible[FileListModel::DateModifiedColumn]);
    EXPECT_TRUE(loaded.visible[FileListModel::GitStatusColumn]);
    EXPECT_FALSE(loaded.visible[FileListModel::TagsColumn]);
    EXPECT_TRUE(loaded.visible[FileListModel::DateCreatedColumn]);
}

TEST_F(ColumnVisibilitySettingsStoreTest, LoadOnMalformedJsonReturnsAllVisibleDefaults)
{
    {
        std::ofstream file(m_settingsFilePath);
        file << "{ not valid json";
    }

    ColumnVisibilitySettingsStore store(m_settingsFilePath);
    const ColumnVisibilitySettings settings = store.load();

    EXPECT_TRUE(settings.visible[FileListModel::SizeColumn]);
    EXPECT_TRUE(settings.visible[FileListModel::TagsColumn]);
}

TEST_F(ColumnVisibilitySettingsStoreTest, LoadIgnoresAnUnrecognizedColumnKeyAndKeepsDefaultsForKnownColumns)
{
    {
        std::ofstream file(m_settingsFilePath);
        file << R"({ "version": 1, "columns": { "Bogus": false } })";
    }

    ColumnVisibilitySettingsStore store(m_settingsFilePath);
    const ColumnVisibilitySettings settings = store.load();

    EXPECT_TRUE(settings.visible[FileListModel::SizeColumn]);
    EXPECT_TRUE(settings.visible[FileListModel::TagsColumn]);
}
