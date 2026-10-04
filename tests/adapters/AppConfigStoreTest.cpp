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

        // A mix of true/false/nullopt hiddenColumns entries (Architecture.md §14.49) on the first
        // tab, exercising the JSON null <-> nullopt mapping; the second tab's array stays all
        // default-constructed nullopt, exercising the "never touched" case in the same round trip.
        std::array<std::optional<bool>, FileListModel::ColumnCount> mixedHiddenColumns{};
        mixedHiddenColumns[FileListModel::SizeColumn] = true;
        mixedHiddenColumns[FileListModel::TypeColumn] = false;

        workspace.panes[static_cast<size_t>(WorkspacePaneId::PaneA)].activeTabIndex = 1;
        workspace.panes[static_cast<size_t>(WorkspacePaneId::PaneA)].tabs = {
            TabConfig{ fs::path("C:/Users/example/Documents"), ViewMode::Tiles, SortCriterion::Size, false, mixedHiddenColumns },
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
    for (int width : config.detailsColumnWidths)
    {
        EXPECT_EQ(width, 0);
    }
    for (int logicalColumn : config.detailsColumnOrder)
    {
        EXPECT_EQ(logicalColumn, 0);
    }
    EXPECT_FALSE(config.leftPanel.expanded);
    EXPECT_EQ(config.leftPanel.activeTabIndex, -1);
    EXPECT_EQ(config.leftPanel.width, 0);
    EXPECT_FALSE(config.rightPanel.expanded);
    EXPECT_EQ(config.rightPanel.activeTabIndex, -1);
    EXPECT_EQ(config.rightPanel.width, 0);
}

TEST_F(AppConfigStoreTest, SaveThenLoadRoundTripsWindowGeometryAndWorkspace)
{
    AppConfigStore store{ m_configPath };

    AppConfig original;
    original.windowGeometry = QByteArray("not-real-geometry-bytes");
    original.workspace = sampleWorkspaceConfig();
    original.detailsColumnWidths = { 220, 90, 130, 160 };
    original.detailsColumnOrder = { 2, 0, 1, 3 };
    original.leftPanel = SidePanelState{ true, 1, 340 };
    original.rightPanel = SidePanelState{ true, 2, 410 };

    ASSERT_TRUE(store.save(original));
    ASSERT_TRUE(fs::exists(m_configPath));

    const AppConfig loaded = store.load();

    EXPECT_EQ(loaded.windowGeometry, original.windowGeometry);
    EXPECT_EQ(loaded.workspace.layout, original.workspace.layout);
    EXPECT_EQ(loaded.workspace.focusedPane, original.workspace.focusedPane);
    EXPECT_EQ(loaded.detailsColumnWidths, original.detailsColumnWidths);
    EXPECT_EQ(loaded.detailsColumnOrder, original.detailsColumnOrder);
    EXPECT_EQ(loaded.leftPanel.expanded, original.leftPanel.expanded);
    EXPECT_EQ(loaded.leftPanel.activeTabIndex, original.leftPanel.activeTabIndex);
    EXPECT_EQ(loaded.leftPanel.width, original.leftPanel.width);
    EXPECT_EQ(loaded.rightPanel.expanded, original.rightPanel.expanded);
    EXPECT_EQ(loaded.rightPanel.activeTabIndex, original.rightPanel.activeTabIndex);
    EXPECT_EQ(loaded.rightPanel.width, original.rightPanel.width);

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
            EXPECT_EQ(loadedPane.tabs[t].hiddenColumns, originalPane.tabs[t].hiddenColumns);
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
    // Missing hiddenColumns falls back to all-nullopt ("never explicitly toggled").
    for (const std::optional<bool>& hidden : paneA.tabs[0].hiddenColumns)
    {
        EXPECT_FALSE(hidden.has_value());
    }
    // Missing detailsColumnWidths falls back to all-zero ("unset").
    for (int width : config.detailsColumnWidths)
    {
        EXPECT_EQ(width, 0);
    }
    // Missing detailsColumnOrder falls back to all-zero ("unset").
    for (int logicalColumn : config.detailsColumnOrder)
    {
        EXPECT_EQ(logicalColumn, 0);
    }
    // Missing leftPanel/rightPanel keys fall back to SidePanelState defaults.
    EXPECT_FALSE(config.leftPanel.expanded);
    EXPECT_EQ(config.leftPanel.activeTabIndex, -1);
    EXPECT_EQ(config.leftPanel.width, 0);
    EXPECT_FALSE(config.rightPanel.expanded);
    EXPECT_EQ(config.rightPanel.activeTabIndex, -1);
    EXPECT_EQ(config.rightPanel.width, 0);
}

TEST_F(AppConfigStoreTest, LoadOnShortDetailsColumnWidthsArrayFillsRemainingColumnsWithZero)
{
    {
        std::ofstream stream(m_configPath, std::ios::binary);
        stream << R"({ "version": 1, "detailsColumnWidths": [240, 100] })";
    }

    AppConfigStore store{ m_configPath };
    const AppConfig config = store.load();

    EXPECT_EQ(config.detailsColumnWidths[0], 240);
    EXPECT_EQ(config.detailsColumnWidths[1], 100);
    EXPECT_EQ(config.detailsColumnWidths[2], 0);
    EXPECT_EQ(config.detailsColumnWidths[3], 0);
}

TEST_F(AppConfigStoreTest, LoadOnHiddenColumnsArrayMapsNullAndFallsBackPerMalformedEntry)
{
    {
        std::ofstream stream(m_configPath, std::ios::binary);
        // NameColumn=null, SizeColumn=true, TypeColumn=false, DateModifiedColumn=null (-> nullopt),
        // GitStatusColumn wrong type (-> nullopt); TagsColumn/DateCreatedColumn/PercentColumn
        // missing entirely (short array -> nullopt).
        stream << R"({
            "version": 1,
            "workspace": {
                "panes": {
                    "PaneA": { "tabs": [
                        { "path": "C:/ok", "hiddenColumns": [null, true, false, null, "not-a-bool"] }
                    ]}
                }
            }
        })";
    }

    AppConfigStore store{ m_configPath };
    const AppConfig config = store.load();

    const PaneConfig& paneA = config.workspace.panes[static_cast<size_t>(WorkspacePaneId::PaneA)];
    ASSERT_EQ(paneA.tabs.size(), 1u);
    const auto& hiddenColumns = paneA.tabs[0].hiddenColumns;

    EXPECT_FALSE(hiddenColumns[FileListModel::NameColumn].has_value());
    ASSERT_TRUE(hiddenColumns[FileListModel::SizeColumn].has_value());
    EXPECT_TRUE(*hiddenColumns[FileListModel::SizeColumn]);
    ASSERT_TRUE(hiddenColumns[FileListModel::TypeColumn].has_value());
    EXPECT_FALSE(*hiddenColumns[FileListModel::TypeColumn]);
    EXPECT_FALSE(hiddenColumns[FileListModel::DateModifiedColumn].has_value());
    EXPECT_FALSE(hiddenColumns[FileListModel::GitStatusColumn].has_value());
    EXPECT_FALSE(hiddenColumns[FileListModel::TagsColumn].has_value());
    EXPECT_FALSE(hiddenColumns[FileListModel::DateCreatedColumn].has_value());
    EXPECT_FALSE(hiddenColumns[FileListModel::PercentColumn].has_value());
}

TEST_F(AppConfigStoreTest, LoadOnShortDetailsColumnOrderArrayFillsRemainingColumnsWithZero)
{
    {
        std::ofstream stream(m_configPath, std::ios::binary);
        stream << R"({ "version": 1, "detailsColumnOrder": [3, 1] })";
    }

    AppConfigStore store{ m_configPath };
    const AppConfig config = store.load();

    EXPECT_EQ(config.detailsColumnOrder[0], 3);
    EXPECT_EQ(config.detailsColumnOrder[1], 1);
    EXPECT_EQ(config.detailsColumnOrder[2], 0);
    EXPECT_EQ(config.detailsColumnOrder[3], 0);
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
                                             SortCriterion::FileType, SortCriterion::Percent));
