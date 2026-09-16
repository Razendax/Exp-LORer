#include <gtest/gtest.h>

#include <fstream>

#include "AppConfigStore.h"

namespace
{
    namespace fs = std::filesystem;

    class AppConfigStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_app_config_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_configPath = m_tempDir / "config.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_configPath;
    };

    WorkspaceConfig sampleWorkspaceConfig()
    {
        WorkspaceConfig workspace;
        workspace.layout = SplitLayout::FourGrid;
        workspace.focusedPane = WorkspacePaneId::PaneC;

        workspace.panes[static_cast<size_t>(WorkspacePaneId::PaneA)].activeTabIndex = 1;
        workspace.panes[static_cast<size_t>(WorkspacePaneId::PaneA)].tabs = {
            TabConfig{ fs::path("C:/Users/example/Documents"), ViewMode::Tiles, SortCriterion::Size, false },
            TabConfig{ fs::path("C:/Users/example/Downloads"), ViewMode::List, SortCriterion::FileType, true },
        };

        workspace.panes[static_cast<size_t>(WorkspacePaneId::PaneC)].tabs = {
            TabConfig{ fs::path("C:/Windows"), ViewMode::Details, SortCriterion::ModificationDate, false },
        };

        return workspace;
    }
}

TEST_F(AppConfigStoreTest, LoadOnMissingFileReturnsDefaults)
{
    AppConfigStore store{ m_configPath };

    const AppConfig config = store.load();

    EXPECT_TRUE(config.windowGeometry.isEmpty());
    EXPECT_EQ(config.workspace.layout, SplitLayout::Single);
    EXPECT_EQ(config.workspace.focusedPane, WorkspacePaneId::PaneA);
    for (const PaneConfig& pane : config.workspace.panes)
    {
        EXPECT_TRUE(pane.tabs.empty());
    }
}

TEST_F(AppConfigStoreTest, SaveThenLoadRoundTripsWindowGeometryAndWorkspace)
{
    AppConfigStore store{ m_configPath };

    AppConfig original;
    original.windowGeometry = QByteArray("not-real-geometry-bytes");
    original.workspace = sampleWorkspaceConfig();

    ASSERT_TRUE(store.save(original));
    ASSERT_TRUE(fs::exists(m_configPath));

    const AppConfig loaded = store.load();

    EXPECT_EQ(loaded.windowGeometry, original.windowGeometry);
    EXPECT_EQ(loaded.workspace.layout, original.workspace.layout);
    EXPECT_EQ(loaded.workspace.focusedPane, original.workspace.focusedPane);

    for (size_t i = 0; i < loaded.workspace.panes.size(); ++i)
    {
        const PaneConfig& loadedPane = loaded.workspace.panes[i];
        const PaneConfig& originalPane = original.workspace.panes[i];

        EXPECT_EQ(loadedPane.activeTabIndex, originalPane.activeTabIndex);
        ASSERT_EQ(loadedPane.tabs.size(), originalPane.tabs.size());

        for (size_t t = 0; t < loadedPane.tabs.size(); ++t)
        {
            EXPECT_EQ(loadedPane.tabs[t].path, originalPane.tabs[t].path);
            EXPECT_EQ(loadedPane.tabs[t].viewMode, originalPane.tabs[t].viewMode);
            EXPECT_EQ(loadedPane.tabs[t].sortCriterion, originalPane.tabs[t].sortCriterion);
            EXPECT_EQ(loadedPane.tabs[t].sortAscending, originalPane.tabs[t].sortAscending);
        }
    }
}

TEST_F(AppConfigStoreTest, SaveDoesNotLeaveAnyTemporaryFileBehind)
{
    AppConfigStore store{ m_configPath };

    ASSERT_TRUE(store.save(AppConfig{}));

    ASSERT_TRUE(fs::exists(m_configPath));

    // The atomic write (temp file + rename) must leave exactly the final config.json in the
    // directory — no stray temp file regardless of the intermediate name it used.
    int entryCount = 0;
    for (const auto& entry : fs::directory_iterator(m_tempDir))
    {
        (void)entry;
        ++entryCount;
    }
    EXPECT_EQ(entryCount, 1);
}

TEST_F(AppConfigStoreTest, LoadOnUnparsableJsonReturnsDefaultsRatherThanCrashing)
{
    {
        std::ofstream stream(m_configPath, std::ios::binary);
        stream << "{ this is not valid json ";
    }

    AppConfigStore store{ m_configPath };
    const AppConfig config = store.load();

    EXPECT_TRUE(config.windowGeometry.isEmpty());
    EXPECT_EQ(config.workspace.layout, SplitLayout::Single);
}

TEST_F(AppConfigStoreTest, LoadOnPartialOrMalformedFieldsFallsBackPerField)
{
    {
        std::ofstream stream(m_configPath, std::ios::binary);
        stream << R"({
            "version": 1,
            "workspace": {
                "layout": "NotARealLayout",
                "panes": {
                    "PaneA": { "activeTabIndex": "not-a-number", "tabs": [
                        { "path": "C:/ok", "viewMode": "NotARealViewMode", "sortCriterion": "Name" }
                    ]}
                }
            }
        })";
    }

    AppConfigStore store{ m_configPath };
    const AppConfig config = store.load();

    // Unknown layout string falls back to the WorkspaceConfig default.
    EXPECT_EQ(config.workspace.layout, SplitLayout::Single);

    const PaneConfig& paneA = config.workspace.panes[static_cast<size_t>(WorkspacePaneId::PaneA)];
    ASSERT_EQ(paneA.tabs.size(), 1u);
    EXPECT_EQ(paneA.tabs[0].path, fs::path("C:/ok"));
    // Unknown viewMode string falls back to TabConfig's default.
    EXPECT_EQ(paneA.tabs[0].viewMode, ViewMode::Details);
    EXPECT_EQ(paneA.tabs[0].sortCriterion, SortCriterion::Name);
    // Missing sortAscending falls back to TabConfig's default (true).
    EXPECT_TRUE(paneA.tabs[0].sortAscending);
}

class AppConfigStoreSplitLayoutRoundTripTest : public AppConfigStoreTest, public ::testing::WithParamInterface<SplitLayout>
{
};

TEST_P(AppConfigStoreSplitLayoutRoundTripTest, RoundTripsThroughSaveAndLoad)
{
    AppConfigStore store{ m_configPath };

    AppConfig config;
    config.workspace.layout = GetParam();

    ASSERT_TRUE(store.save(config));
    EXPECT_EQ(store.load().workspace.layout, GetParam());
}

INSTANTIATE_TEST_SUITE_P(AllValues, AppConfigStoreSplitLayoutRoundTripTest,
                          ::testing::Values(SplitLayout::Single, SplitLayout::TwoVertical, SplitLayout::TwoHorizontal,
                                             SplitLayout::FourGrid));

class AppConfigStoreViewModeRoundTripTest : public AppConfigStoreTest, public ::testing::WithParamInterface<ViewMode>
{
};

TEST_P(AppConfigStoreViewModeRoundTripTest, RoundTripsThroughSaveAndLoad)
{
    AppConfigStore store{ m_configPath };

    AppConfig config;
    config.workspace.panes[0].tabs = { TabConfig{ fs::path("C:/x"), GetParam(), SortCriterion::Name, true } };

    ASSERT_TRUE(store.save(config));
    EXPECT_EQ(store.load().workspace.panes[0].tabs.at(0).viewMode, GetParam());
}

INSTANTIATE_TEST_SUITE_P(AllValues, AppConfigStoreViewModeRoundTripTest,
                          ::testing::Values(ViewMode::ExtraLargeIcons, ViewMode::LargeIcons, ViewMode::MediumIcons,
                                             ViewMode::SmallIcons, ViewMode::List, ViewMode::Details, ViewMode::Tiles));

class AppConfigStoreSortCriterionRoundTripTest : public AppConfigStoreTest, public ::testing::WithParamInterface<SortCriterion>
{
};

TEST_P(AppConfigStoreSortCriterionRoundTripTest, RoundTripsThroughSaveAndLoad)
{
    AppConfigStore store{ m_configPath };

    AppConfig config;
    config.workspace.panes[0].tabs = { TabConfig{ fs::path("C:/x"), ViewMode::Details, GetParam(), true } };

    ASSERT_TRUE(store.save(config));
    EXPECT_EQ(store.load().workspace.panes[0].tabs.at(0).sortCriterion, GetParam());
}

INSTANTIATE_TEST_SUITE_P(AllValues, AppConfigStoreSortCriterionRoundTripTest,
                          ::testing::Values(SortCriterion::Name, SortCriterion::Size, SortCriterion::ModificationDate,
                                             SortCriterion::FileType));
