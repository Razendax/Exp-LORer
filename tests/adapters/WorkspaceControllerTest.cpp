#include <gtest/gtest.h>

#include <fstream>

#include "FileNavigationUseCase.h"
#include "SQLiteTagRepository.h"
#include "ShellContextMenuProvider.h"
#include "StandardFileSystemRepository.h"
#include "TabViewModel.h"
#include "TagManagementUseCase.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    namespace fs = std::filesystem;

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    // Builds a real WorkspaceController following CompositionRoot's own wiring (Architecture.md
    // §11: adapter-layer tests use real files/temp dirs/an in-memory DB, no mocks).
    class WorkspaceControllerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_workspace_controller_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        StandardFileSystemRepository m_fileSystemRepository;
        ShellContextMenuProvider m_contextMenuProvider;
        FileNavigationUseCase m_fileNavigationUseCase{ m_fileSystemRepository, m_contextMenuProvider };
        SQLiteTagRepository m_tagRepository{ fs::path(":memory:") };
        TagManagementUseCase m_tagManagementUseCase{ m_tagRepository, m_fileSystemRepository };
        WorkspaceController m_controller{ m_fileNavigationUseCase, m_tagManagementUseCase };
    };
}

TEST_F(WorkspaceControllerTest, CaptureConfigRoundTripsThroughRestoreFromConfig)
{
    writeFile(m_tempDir / "a.txt", "hello");

    WorkspacePaneViewModel* paneA = m_controller.pane(WorkspacePaneId::PaneA);
    TabViewModel* tabA0 = paneA->addTab();
    tabA0->navigateTo(m_tempDir);
    tabA0->setViewMode(ViewMode::Tiles);
    tabA0->setSortCriterion(SortCriterion::Size, false);

    TabViewModel* tabA1 = paneA->addTab();
    tabA1->navigateTo(m_tempDir);
    paneA->setActiveTab(1);

    WorkspacePaneViewModel* paneB = m_controller.pane(WorkspacePaneId::PaneB);
    TabViewModel* tabB0 = paneB->addTab();
    tabB0->navigateTo(m_tempDir);
    tabB0->setViewMode(ViewMode::List);
    tabB0->setSortCriterion(SortCriterion::FileType, true);

    m_controller.setLayout(SplitLayout::TwoVertical);
    m_controller.setFocusedPane(WorkspacePaneId::PaneB);

    const WorkspaceConfig captured = m_controller.captureConfig();

    // A fresh controller (nothing seeded yet) restored from the captured snapshot.
    SQLiteTagRepository restoredTagRepository{ fs::path(":memory:") };
    TagManagementUseCase restoredTagManagementUseCase{ restoredTagRepository, m_fileSystemRepository };
    WorkspaceController restoredController{ m_fileNavigationUseCase, restoredTagManagementUseCase };

    const bool restored = restoredController.restoreFromConfig(captured);
    ASSERT_TRUE(restored);

    EXPECT_EQ(restoredController.layout(), SplitLayout::TwoVertical);
    EXPECT_EQ(restoredController.focusedPane(), WorkspacePaneId::PaneB);

    WorkspacePaneViewModel* restoredPaneA = restoredController.pane(WorkspacePaneId::PaneA);
    ASSERT_EQ(restoredPaneA->tabCount(), 2);
    EXPECT_EQ(restoredPaneA->activeIndex(), 1);
    EXPECT_EQ(restoredPaneA->tabAt(0)->currentPath(), m_tempDir);
    EXPECT_EQ(restoredPaneA->tabAt(0)->viewMode(), ViewMode::Tiles);
    EXPECT_EQ(restoredPaneA->tabAt(0)->sortCriterion(), SortCriterion::Size);
    EXPECT_FALSE(restoredPaneA->tabAt(0)->sortAscending());

    WorkspacePaneViewModel* restoredPaneB = restoredController.pane(WorkspacePaneId::PaneB);
    ASSERT_EQ(restoredPaneB->tabCount(), 1);
    EXPECT_EQ(restoredPaneB->tabAt(0)->viewMode(), ViewMode::List);
    EXPECT_EQ(restoredPaneB->tabAt(0)->sortCriterion(), SortCriterion::FileType);
    EXPECT_TRUE(restoredPaneB->tabAt(0)->sortAscending());

    WorkspacePaneViewModel* restoredPaneC = restoredController.pane(WorkspacePaneId::PaneC);
    EXPECT_EQ(restoredPaneC->tabCount(), 0);
}

TEST_F(WorkspaceControllerTest, RestoreFromConfigWithNoTabsReturnsFalse)
{
    const WorkspaceConfig empty;

    EXPECT_FALSE(m_controller.restoreFromConfig(empty));
}

TEST_F(WorkspaceControllerTest, RestoreFromConfigWithDeletedPathDegradesToNavigationFailed)
{
    const fs::path deletedPath = m_tempDir / "no-longer-here";

    WorkspaceConfig config;
    config.panes[static_cast<size_t>(WorkspacePaneId::PaneA)].tabs.push_back(
        TabConfig{ deletedPath, ViewMode::Details, SortCriterion::Name, true });

    // The tab is still created (restoreFromConfig counts tab creation, not navigation success) —
    // navigateTo just fails the same way it would for any other missing path, leaving the tab on
    // an empty listing rather than crashing.
    ASSERT_TRUE(m_controller.restoreFromConfig(config));

    TabViewModel* tab = m_controller.pane(WorkspacePaneId::PaneA)->tabAt(0);
    ASSERT_NE(tab, nullptr);

    bool navigationFailedEmitted = false;
    QObject::connect(tab, &TabViewModel::navigationFailed, [&navigationFailedEmitted]() { navigationFailedEmitted = true; });

    // Re-navigate to the same (still missing) path to observe the failure signal directly, since
    // restoreFromConfig's own navigateTo call already happened before this test could connect.
    tab->navigateTo(deletedPath);

    EXPECT_TRUE(navigationFailedEmitted);
    EXPECT_TRUE(tab->currentPath().empty());
}
