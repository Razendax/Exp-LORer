#include <gtest/gtest.h>

#include <fstream>

#include "AdvancedSearchService.h"
#include "ContentSearchService.h"
#include "ContentSearchUseCase.h"
#include "DirectoryWatchUseCase.h"
#include "DiskUsageService.h"
#include "FileDecorationRules.h"
#include "FileDecorationRulesStore.h"
#include "FileDecorationsViewModel.h"
#include "FileNavigationUseCase.h"
#include "FilePreviewUseCase.h"
#include "FileOperationsController.h"
#include "FileTagsService.h"
#include "FileTransferService.h"
#include "FolderMonitorUseCase.h"
#include "FolderTreeModel.h"
#include "FragmentationService.h"
#include "GitBranchUseCase.h"
#include "GitStatusService.h"
#include "GitStatusUseCase.h"
#include "IconLoadingService.h"
#include "LibGit2BranchProvider.h"
#include "LibGit2StatusProvider.h"
#include "MediaDecoder.h"
#include "MediaProcessingUseCase.h"
#include "MediaThumbnailLoadingService.h"
#include "NotificationCenter.h"
#include "NotificationSettings.h"
#include "SQLiteTagRepository.h"
#include "ShellContextMenuProvider.h"
#include "StandardDirectoryWatcher.h"
#include "StandardFileSystemRepository.h"
#include "StandardFolderMonitor.h"
#include "TabViewModel.h"
#include "TagChangeNotifier.h"
#include "TagManagementUseCase.h"
#include "WindowsDiskUsageScanner.h"
#include "WindowsFragmentationScanner.h"
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
        MediaDecoder m_mediaDecoder;
        FilePreviewUseCase m_filePreviewUseCase{ m_fileSystemRepository, m_mediaDecoder };
        FileDecorationRulesStore m_fileDecorationRulesStore{ fs::temp_directory_path() /
                                                               "explorer_workspace_controller_test_decorations.json" };
        FileDecorationRules m_fileDecorationRules;
        FileDecorationsViewModel m_fileDecorationsViewModel{ m_fileDecorationRules, m_fileDecorationRulesStore };
        LibGit2StatusProvider m_gitStatusProvider;
        GitStatusUseCase m_gitStatusUseCase{ m_gitStatusProvider };
        LibGit2BranchProvider m_gitBranchProvider;
        GitBranchUseCase m_gitBranchUseCase{ m_gitBranchProvider };
        GitStatusService m_gitStatusService{ m_gitStatusUseCase, m_gitBranchUseCase };
        StandardDirectoryWatcher m_directoryWatcher;
        DirectoryWatchUseCase m_directoryWatchUseCase{ m_directoryWatcher };
        StandardFolderMonitor m_folderMonitor;
        FolderMonitorUseCase m_folderMonitorUseCase{ m_folderMonitor };
        NotificationSettings m_notificationSettings;
        NotificationCenter m_notificationCenter{ m_notificationSettings };
        AdvancedSearchService m_advancedSearchService{ m_fileNavigationUseCase, m_fileSystemRepository, fs::path(":memory:") };
        ContentSearchUseCase m_contentSearchUseCase{ m_fileNavigationUseCase, m_fileSystemRepository };
        ContentSearchService m_contentSearchService{ m_fileNavigationUseCase, m_fileSystemRepository, m_contentSearchUseCase };
        FileTagsService m_fileTagsService{ m_fileSystemRepository, fs::path(":memory:") };
        WindowsDiskUsageScanner m_diskUsageScanner;
        DiskUsageService m_diskUsageService{ m_diskUsageScanner, fs::path(":memory:") };
        WindowsFragmentationScanner m_fragmentationScanner{ m_fileSystemRepository };
        FragmentationService m_fragmentationService{ m_fragmentationScanner, fs::path(":memory:") };
        IconLoadingService m_iconLoadingService;
        MediaProcessingUseCase m_mediaProcessingUseCase{ m_mediaDecoder };
        MediaThumbnailLoadingService m_mediaThumbnailLoadingService{ m_mediaProcessingUseCase };
        TagChangeNotifier m_tagChangeNotifier;
        FileTransferService m_fileTransferService{ m_fileSystemRepository };
        WorkspaceController m_controller{ m_fileNavigationUseCase,  m_tagManagementUseCase,     m_filePreviewUseCase,
                                           m_fileSystemRepository, m_fileDecorationRules,      m_fileDecorationsViewModel,
                                           m_gitStatusService,     m_directoryWatchUseCase,    m_folderMonitorUseCase,
                                           m_notificationCenter,   m_advancedSearchService,    m_contentSearchService,
                                           m_fileTagsService,      m_diskUsageService,         m_fragmentationService,
                                           m_iconLoadingService,   m_mediaThumbnailLoadingService, m_tagChangeNotifier,
                                           m_fileTransferService };
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
    WorkspaceController restoredController{ m_fileNavigationUseCase,  restoredTagManagementUseCase, m_filePreviewUseCase,
                                             m_fileSystemRepository, m_fileDecorationRules,        m_fileDecorationsViewModel,
                                             m_gitStatusService,     m_directoryWatchUseCase,      m_folderMonitorUseCase,
                                             m_notificationCenter,   m_advancedSearchService,      m_contentSearchService,
                                             m_fileTagsService,      m_diskUsageService,           m_fragmentationService,
                                             m_iconLoadingService,   m_mediaThumbnailLoadingService, m_tagChangeNotifier,
                                             m_fileTransferService };

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

TEST_F(WorkspaceControllerTest, CaptureConfigRoundTripsHiddenColumnsThroughRestoreFromConfig)
{
    writeFile(m_tempDir / "a.txt", "hello");

    WorkspacePaneViewModel* paneA = m_controller.pane(WorkspacePaneId::PaneA);
    TabViewModel* tabA0 = paneA->addTab();
    tabA0->navigateTo(m_tempDir);
    tabA0->setColumnHidden(FileListModel::SizeColumn, true);
    tabA0->setColumnHidden(FileListModel::TypeColumn, false);
    // DateModifiedColumn is deliberately left untouched -- its slot should stay nullopt.

    const WorkspaceConfig captured = m_controller.captureConfig();

    SQLiteTagRepository restoredTagRepository{ fs::path(":memory:") };
    TagManagementUseCase restoredTagManagementUseCase{ restoredTagRepository, m_fileSystemRepository };
    WorkspaceController restoredController{ m_fileNavigationUseCase,  restoredTagManagementUseCase, m_filePreviewUseCase,
                                             m_fileSystemRepository, m_fileDecorationRules,        m_fileDecorationsViewModel,
                                             m_gitStatusService,     m_directoryWatchUseCase,      m_folderMonitorUseCase,
                                             m_notificationCenter,   m_advancedSearchService,      m_contentSearchService,
                                             m_fileTagsService,      m_diskUsageService,           m_fragmentationService,
                                             m_iconLoadingService,   m_mediaThumbnailLoadingService, m_tagChangeNotifier,
                                             m_fileTransferService };

    ASSERT_TRUE(restoredController.restoreFromConfig(captured));

    TabViewModel* restoredTab = restoredController.pane(WorkspacePaneId::PaneA)->tabAt(0);
    ASSERT_NE(restoredTab, nullptr);

    const auto& hiddenColumns = restoredTab->hiddenColumns();
    ASSERT_TRUE(hiddenColumns[FileListModel::SizeColumn].has_value());
    EXPECT_TRUE(*hiddenColumns[FileListModel::SizeColumn]);
    ASSERT_TRUE(hiddenColumns[FileListModel::TypeColumn].has_value());
    EXPECT_FALSE(*hiddenColumns[FileListModel::TypeColumn]);
    EXPECT_FALSE(hiddenColumns[FileListModel::DateModifiedColumn].has_value());
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

TEST_F(WorkspaceControllerTest, RenamingAnAncestorFolderMovesTabsBrowsingInsideIt)
{
    fs::create_directories(m_tempDir / "project" / "src");

    TabViewModel* inside = m_controller.pane(WorkspacePaneId::PaneA)->addTab();
    inside->navigateTo(m_tempDir / "project" / "src");
    TabViewModel* otherPane = m_controller.pane(WorkspacePaneId::PaneB)->addTab();
    otherPane->navigateTo(m_tempDir / "project");
    TabViewModel* outside = m_controller.pane(WorkspacePaneId::PaneB)->addTab();
    outside->navigateTo(m_tempDir);

    m_controller.fileOperationsController()->renamePath(m_tempDir / "project", m_tempDir / "renamed");

    EXPECT_EQ(inside->currentPath(), m_tempDir / "renamed" / "src");
    EXPECT_EQ(otherPane->currentPath(), m_tempDir / "renamed");
    EXPECT_EQ(outside->currentPath(), m_tempDir);
    // Replaced in place, not recorded as a new navigation step.
    EXPECT_FALSE(inside->backAvailable());
}

TEST_F(WorkspaceControllerTest, DeletingAFolderMovesTabsBrowsingInsideItToItsParent)
{
    fs::create_directories(m_tempDir / "doomed" / "inner");

    TabViewModel* inner = m_controller.pane(WorkspacePaneId::PaneA)->addTab();
    inner->navigateTo(m_tempDir / "doomed" / "inner");
    TabViewModel* atFolder = m_controller.pane(WorkspacePaneId::PaneA)->addTab();
    atFolder->navigateTo(m_tempDir / "doomed");

    // Permanent delete, so the test never touches the real Recycle Bin.
    m_controller.fileOperationsController()->deletePermanently(m_tempDir / "doomed");

    EXPECT_EQ(inner->currentPath(), m_tempDir);
    EXPECT_EQ(atFolder->currentPath(), m_tempDir);
}

TEST_F(WorkspaceControllerTest, FolderTreeFollowsTheFocusedTabsPath)
{
    fs::create_directories(m_tempDir / "sub");
    FolderTreeModel* tree = m_controller.folderTreeModel();

    std::vector<fs::path> revealed;
    QObject::connect(tree, &FolderTreeModel::activePathChanged, [&revealed](const fs::path& path) { revealed.push_back(path); });

    TabViewModel* tabA = m_controller.pane(WorkspacePaneId::PaneA)->addTab();
    tabA->navigateTo(m_tempDir);
    EXPECT_EQ(tree->activePath(), m_tempDir);

    tabA->navigateTo(m_tempDir / "sub");
    EXPECT_EQ(tree->activePath(), m_tempDir / "sub");

    // A tab in an unfocused pane doesn't drive the tree...
    TabViewModel* tabB = m_controller.pane(WorkspacePaneId::PaneB)->addTab();
    tabB->navigateTo(m_tempDir);
    EXPECT_EQ(tree->activePath(), m_tempDir / "sub");

    // ...until its pane gets focus.
    m_controller.setFocusedPane(WorkspacePaneId::PaneB);
    EXPECT_EQ(tree->activePath(), m_tempDir);

    ASSERT_FALSE(revealed.empty());
    EXPECT_EQ(revealed.back(), m_tempDir);
}
