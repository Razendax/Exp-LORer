#include "MainWindow.h"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QProcess>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include "AdvancedSearchService.h"
#include "AppConfigStore.h"
#include "BulkRenameDialog.h"
#include "BulkRenameUseCase.h"
#include "BulkRenameViewModel.h"
#include "ContentSearchPanelWidget.h"
#include "DriveSpaceInfoWorker.h"
#include "DuplicateFinderDialog.h"
#include "EnvironmentVariablesDialog.h"
#include "FavoritesMenuController.h"
#include "FavoritesViewModel.h"
#include "FileBrowserView.h"
#include "FilePreviewViewModel.h"
#include "FileDecorationsViewModel.h"
#include "FileItemContextMenu.h"
#include "FileOperationsController.h"
#include "FolderTreeModel.h"
#include "FolderTreeView.h"
#include "HotkeyManager.h"
#include "HotkeyWindowDispatcher.h"
#include "ManageFavoritesDialog.h"
#include "MainStatusBarWidget.h"
#include "MediaViewerModeController.h"
#include "MediaViewerSearchPanelWidget.h"
#include "MonitoringPanelWidget.h"
#include "NotificationOverlayWidget.h"
#include "SettingsDialog.h"
#include "TabAdvancedSearchViewModel.h"
#include "TabContentSearchViewModel.h"
#include "TabMonitoringViewModel.h"
#include "TabQuickSearchViewModel.h"
#include "TabViewModel.h"
#include "TagManagerDialog.h"
#include "TagManagerViewModel.h"
#include "TextEditorDialog.h"
#include "TextEditorOptionsDialog.h"
#include "PreviewPanelWidget.h"
#include "PathContainment.h"
#include "SearchPanelWidget.h"
#include "SidePanelWidget.h"
#include "TagPanelWidget.h"
#include "VirtualPaths.h"
#include "WorkspaceController.h"
#include "WorkspaceLayoutTopology.h"
#include "WorkspaceLayoutWidget.h"
#include "WorkspaceManagerDialog.h"
#include "WorkspacePaneId.h"
#include "WorkspacePaneViewModel.h"
#include "WorkspacePaneWidget.h"
#include "WorkspaceStore.h"

namespace
{
    // Order shared with MainWindow::m_layoutActions: element i of one is the action for element
    // i of the other.
    constexpr std::array<SplitLayout, 4> kLayouts = {
        SplitLayout::Single,
        SplitLayout::TwoVertical,
        SplitLayout::TwoHorizontal,
        SplitLayout::FourGrid,
    };

    constexpr std::array<WorkspacePaneId, 4> kAllPanes = {
        WorkspacePaneId::PaneA,
        WorkspacePaneId::PaneB,
        WorkspacePaneId::PaneC,
        WorkspacePaneId::PaneD,
    };

    // Order shared with MainWindow::m_sortCriterionActions: element i of one is the action for
    // element i of the other.
    constexpr std::array<SortCriterion, 4> kSortCriteria = {
        SortCriterion::Name,
        SortCriterion::Size,
        SortCriterion::FileType,
        SortCriterion::ModificationDate,
    };

    QString layoutLabel(SplitLayout layout)
    {
        switch (layout)
        {
            case SplitLayout::Single:
                return QObject::tr("Single");
            case SplitLayout::TwoVertical:
                return QObject::tr("Split vertically");
            case SplitLayout::TwoHorizontal:
                return QObject::tr("Split horizontally");
            case SplitLayout::FourGrid:
                return QObject::tr("4-pane grid");
        }
        return QString();
    }

    QString sortCriterionLabel(SortCriterion criterion)
    {
        switch (criterion)
        {
            case SortCriterion::Name:
                return QObject::tr("Name");
            case SortCriterion::Size:
                return QObject::tr("Size");
            case SortCriterion::FileType:
                return QObject::tr("Type");
            case SortCriterion::ModificationDate:
                return QObject::tr("Date modified");
            case SortCriterion::Percent:
                return QObject::tr("Percent");
        }
        return QString();
    }
}

MainWindow::MainWindow(WorkspaceController* workspaceController, AppConfigStore& configStore,
                         TagManagementUseCase& tagManagementUseCase, TagChangeNotifier& tagChangeNotifier,
                         AdvancedSearchService& advancedSearchService, IconLoadingService& iconLoadingService,
                         FileTagsService& fileTagsService, TextFileEditViewModel& textFileEditViewModel,
                         UiThemeChangeNotifier& uiThemeChangeNotifier,
                         SyntaxHighlightEngine& syntaxHighlightEngine,
                         HighlightThemeViewModel& highlightThemeViewModel, FileDecorationsViewModel& fileDecorationsViewModel,
                         FavoritesViewModel& favoritesViewModel, NotificationCenter& notificationCenter,
                         NotificationSettingsViewModel& notificationSettingsViewModel,
                         ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel, DiskUsageService& diskUsageService,
                         DuplicateFinderService& duplicateFinderService, FragmentationService& fragmentationService,
                         BulkRenameUseCase& bulkRenameUseCase, EnvironmentVariablesViewModel& environmentVariablesViewModel,
                         HotkeyManager& hotkeyManager, WorkspaceStore& workspaceStore, const WorkspaceLaunchContext& launchContext,
                         const AppConfig& initialConfig, QWidget* parent)
    : QMainWindow(parent)
    , m_workspaceController(workspaceController)
    , m_configStore(configStore)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_advancedSearchService(advancedSearchService)
    , m_iconLoadingService(iconLoadingService)
    , m_fileTagsService(fileTagsService)
    , m_textFileEditViewModel(textFileEditViewModel)
    , m_uiThemeChangeNotifier(uiThemeChangeNotifier)
    , m_syntaxHighlightEngine(syntaxHighlightEngine)
    , m_highlightThemeViewModel(highlightThemeViewModel)
    , m_fileDecorationsViewModel(fileDecorationsViewModel)
    , m_favoritesViewModel(favoritesViewModel)
    , m_notificationCenter(notificationCenter)
    , m_notificationSettingsViewModel(notificationSettingsViewModel)
    , m_columnVisibilitySettingsViewModel(columnVisibilitySettingsViewModel)
    , m_diskUsageService(diskUsageService)
    , m_duplicateFinderService(duplicateFinderService)
    , m_fragmentationService(fragmentationService)
    , m_bulkRenameUseCase(bulkRenameUseCase)
    , m_environmentVariablesViewModel(environmentVariablesViewModel)
    , m_hotkeyManager(hotkeyManager)
    , m_workspaceStore(workspaceStore)
    , m_launchContext(launchContext)
    , m_columnWidths(initialConfig.detailsColumnWidths)
    , m_columnOrder(initialConfig.detailsColumnOrder)
    , m_initialLeftPanelState(initialConfig.leftPanel)
    , m_initialRightPanelState(initialConfig.rightPanel)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Exp-LORer — %1").arg(currentWorkspaceName()));

    if (!initialConfig.windowGeometry.isEmpty())
    {
        restoreGeometry(initialConfig.windowGeometry);
    }
    else
    {
        resize(1024, 768);
    }

    createLayoutActions();
    createSortByActions();
    createShowHiddenFilesAction();
    createMediaViewerModeAction();
    createHoverPreviewAction();
    createMenuBar();
    createLayoutToolBar();
    createWorkspace();
    createMainStatusBar();
    createHotkeyWindowDispatcher();

    m_notificationOverlayWidget = new NotificationOverlayWidget(m_notificationCenter, this);
    m_notificationOverlayWidget->setObjectName(QStringLiteral("notificationOverlayWidget"));
    m_notificationOverlayWidget->reposition(height() - statusBar()->height());

    m_driveInfoWorker = new DriveSpaceInfoWorker(m_workspaceController->fileNavigationUseCase());
    m_driveInfoWorker->moveToThread(&m_driveInfoThread);
    m_driveInfoThread.start();

    connect(m_workspaceController, &WorkspaceController::layoutChanged, this, &MainWindow::onLayoutChanged);
    connect(m_workspaceController, &WorkspaceController::focusedTabChanged, this, &MainWindow::onFocusedTabChanged);

    onLayoutChanged(m_workspaceController->layout());
    onFocusedTabChanged(m_workspaceController->focusedTab());
}

MainWindow::~MainWindow()
{
    m_driveInfoThread.quit();
    m_driveInfoThread.wait();
    delete m_driveInfoWorker;
}

void MainWindow::createLayoutActions()
{
    m_layoutActionGroup = new QActionGroup(this);
    m_layoutActionGroup->setExclusive(true);

    for (SplitLayout layout : kLayouts)
    {
        QAction* action = new QAction(layoutLabel(layout), this);
        action->setCheckable(true);
        m_layoutActionGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, layout]() { m_workspaceController->setLayout(layout); });
        m_layoutActions.append(action);
    }
}

void MainWindow::createSortByActions()
{
    m_sortCriterionActionGroup = new QActionGroup(this);
    m_sortCriterionActionGroup->setExclusive(true);

    for (SortCriterion criterion : kSortCriteria)
    {
        QAction* action = new QAction(sortCriterionLabel(criterion), this);
        action->setCheckable(true);
        m_sortCriterionActionGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, criterion]() {
            TabViewModel* tab = m_workspaceController->focusedTab();
            if (!tab)
            {
                return;
            }
            tab->setSortCriterion(criterion, tab->sortAscending());
        });
        m_sortCriterionActions.append(action);
    }

    m_sortOrderActionGroup = new QActionGroup(this);
    m_sortOrderActionGroup->setExclusive(true);

    m_ascendingAction = new QAction(tr("Ascending"), this);
    m_ascendingAction->setCheckable(true);
    m_sortOrderActionGroup->addAction(m_ascendingAction);
    connect(m_ascendingAction, &QAction::triggered, this, [this]() {
        TabViewModel* tab = m_workspaceController->focusedTab();
        if (!tab)
        {
            return;
        }
        tab->setSortCriterion(tab->sortCriterion(), true);
    });

    m_descendingAction = new QAction(tr("Descending"), this);
    m_descendingAction->setCheckable(true);
    m_sortOrderActionGroup->addAction(m_descendingAction);
    connect(m_descendingAction, &QAction::triggered, this, [this]() {
        TabViewModel* tab = m_workspaceController->focusedTab();
        if (!tab)
        {
            return;
        }
        tab->setSortCriterion(tab->sortCriterion(), false);
    });
}

void MainWindow::createShowHiddenFilesAction()
{
    m_showHiddenFilesAction = new QAction(tr("Show hidden files/folders"), this);
    m_showHiddenFilesAction->setCheckable(true);
    m_showHiddenFilesAction->setChecked(FileListModel::showHiddenFilesEnabled());

    connect(m_showHiddenFilesAction, &QAction::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QLatin1String(FileListModel::kShowHiddenFilesSettingsKey), checked);

        for (WorkspacePaneId id : kAllPanes)
        {
            WorkspacePaneViewModel* pane = m_workspaceController->pane(id);
            for (int i = 0; i < pane->tabCount(); ++i)
            {
                TabViewModel* tab = pane->tabAt(i);
                tab->fileListModel()->setShowHiddenFiles(checked);
                tab->quickSearch()->resultsModel()->setShowHiddenFiles(checked);
                tab->advancedSearch()->resultsModel()->setShowHiddenFiles(checked);
                // Content search excludes hidden content from the scan itself (not just from
                // display) per spec, so a model-level visibility flip isn't enough here -- an
                // active tab needs a full rescan instead.
                if (tab->contentSearch()->isActive())
                {
                    tab->contentSearch()->refresh(tab->contentSearch()->query(), tab->currentPath());
                }
            }
        }
        m_workspaceController->folderTreeModel()->setShowHiddenFiles(checked);
    });
}

void MainWindow::createMediaViewerModeAction()
{
    // Just the bare action: it must exist before createMenuBar() runs, but MediaViewerModeController
    // (which wires its toggled behavior) isn't constructed until createWorkspace() has built every
    // widget it needs (Architecture.md §14.96).
    m_mediaViewerModeAction = new QAction(tr("Media Viewer Mode"), this);
    m_mediaViewerModeAction->setCheckable(true);
    m_mediaViewerModeAction->setEnabled(false); // re-enabled by onFocusedTabChanged once a tab exists
}

void MainWindow::createHoverPreviewAction()
{
    m_hoverPreviewEnabledAction = new QAction(tr("Enable Hover Preview"), this);
    m_hoverPreviewEnabledAction->setCheckable(true);
    m_hoverPreviewEnabledAction->setChecked(FileBrowserView::hoverPreviewEnabled());

    connect(m_hoverPreviewEnabledAction, &QAction::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QLatin1String(FileBrowserView::kHoverPreviewEnabledSettingsKey), checked);
    });
}

void MainWindow::createMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Settings..."), this, &MainWindow::showSettingsDialog);
    fileMenu->addSeparator();

    createWorkspacesMenu(fileMenu);

    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Tag Edit..."), this, &MainWindow::showTagManagerDialog);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    QMenu* layoutMenu = viewMenu->addMenu(tr("&Layout"));
    layoutMenu->addActions(m_layoutActions);

    m_sortByMenu = viewMenu->addMenu(tr("&Sort by"));
    m_sortByMenu->addActions(m_sortCriterionActions);
    m_sortByMenu->addSeparator();
    m_sortByMenu->addAction(m_ascendingAction);
    m_sortByMenu->addAction(m_descendingAction);

    viewMenu->addSeparator();
    viewMenu->addAction(m_showHiddenFilesAction);
    viewMenu->addAction(m_mediaViewerModeAction);

    // A single-item submenu is intentionally future-proof for additional related toggles without
    // a menu-structure change later (Architecture.md §14.76).
    QMenu* hoverPreviewMenu = viewMenu->addMenu(tr("Hover Preview"));
    hoverPreviewMenu->addAction(m_hoverPreviewEnabledAction);

    createFavoritesMenu();
    QMenu* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("Find &Duplicate Files..."), this, &MainWindow::showDuplicateFinderDialog);
    toolsMenu->addAction(tr("&Bulk Rename..."), this, &MainWindow::showBulkRenameDialogFromMenu);
    toolsMenu->addAction(tr("&Environment Variables..."), this, &MainWindow::showEnvironmentVariablesDialog);
    toolsMenu->addAction(tr("&Text Editor..."), this, &MainWindow::showTextEditorDialog);
    menuBar()->addMenu(tr("&Help"));
}

void MainWindow::createFavoritesMenu()
{
    // `paneWidgetResolver` defers the WorkspaceLayoutWidget lookup to when a favorite item is
    // actually clicked: createWorkspace() (which constructs m_workspaceLayoutWidget) hasn't run
    // yet at this point in the constructor (createMenuBar() runs first).
    m_favoritesMenuController = new FavoritesMenuController(
        m_favoritesViewModel, m_workspaceController->fileNavigationUseCase(), *m_workspaceController,
        [this](WorkspacePaneId id) { return m_workspaceLayoutWidget->paneWidget(id); }, this);
    menuBar()->addMenu(m_favoritesMenuController->menu());
    connect(m_favoritesMenuController, &FavoritesMenuController::manageFavoritesRequested, this,
            &MainWindow::showManageFavoritesDialog);
    connect(m_favoritesMenuController, &FavoritesMenuController::statusMessage, this, &MainWindow::onStatusMessage);
}

void MainWindow::createWorkspacesMenu(QMenu* fileMenu)
{
    m_workspacesMenu = fileMenu->addMenu(tr("&Workspaces"));

    // Rebuilt lazily right before the menu is shown, rather than kept live-synced -- same posture
    // FavoritesMenuController's own menu uses (Architecture.md §14.60).
    connect(m_workspacesMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildWorkspacesMenuItems);

    fileMenu->addAction(tr("Save as &Workspace..."), this, &MainWindow::onSaveAsWorkspaceTriggered);
    fileMenu->addAction(tr("Workspace &Management..."), this, &MainWindow::showWorkspaceManagerDialog);
}

void MainWindow::createLayoutToolBar()
{
    QToolBar* toolBar = addToolBar(tr("Layout"));
    toolBar->setObjectName(QStringLiteral("layoutToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar->addActions(m_layoutActions);

    // Order shared with kLayouts: element i of m_layoutActions is the action for element i here.
    static constexpr std::array<const char*, 4> kLayoutButtonIds = {
        "layoutSingleButton",
        "layoutTwoVerticalButton",
        "layoutTwoHorizontalButton",
        "layoutFourGridButton",
    };
    for (int i = 0; i < m_layoutActions.size(); ++i)
    {
        if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(m_layoutActions[i])))
        {
            button->setObjectName(QLatin1String(kLayoutButtonIds[static_cast<size_t>(i)]));
        }
    }
}

void MainWindow::createWorkspace()
{
    m_workspaceLayoutWidget = new WorkspaceLayoutWidget(m_workspaceController, m_columnVisibilitySettingsViewModel,
                                                          m_diskUsageService, m_fragmentationService, m_notificationCenter,
                                                          m_uiThemeChangeNotifier, m_hotkeyManager, m_columnWidths,
                                                          m_columnOrder, this);
    m_workspaceLayoutWidget->setObjectName(QStringLiteral("workspaceLayoutWidget"));

    // Right panel (Architecture.md §14.25): a collapsible side-panel tab strip wrapping the tag UI,
    // file preview and folder monitoring log.
    m_rightPanelWidget = new SidePanelWidget(SidePanelWidget::Edge::Right, QStringLiteral("rightPanel"), m_uiThemeChangeNotifier, this);
    m_tagPanelWidget = new TagPanelWidget(m_workspaceController->tagListViewModel(), m_uiThemeChangeNotifier, m_rightPanelWidget);
    m_tagPanelWidget->setObjectName(QStringLiteral("tagPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Tags"), m_tagPanelWidget, QStringLiteral("rightPanelTagsButton"));

    m_previewPanelWidget = new PreviewPanelWidget(m_workspaceController->filePreviewViewModel(), m_syntaxHighlightEngine,
                                                   m_highlightThemeViewModel, m_textFileEditViewModel, m_rightPanelWidget);
    m_previewPanelWidget->setObjectName(QStringLiteral("previewPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Preview"), m_previewPanelWidget, QStringLiteral("rightPanelPreviewButton"));

    // Find-in-Files line jump (Architecture.md §14.104): added after PreviewPanelWidget's own ctor
    // has already connected its onPreviewReady handler to the same signal, so Qt's direct-connection
    // ordering guarantees that handler runs first -- the editor/content is already populated by the
    // time this one fires.
    connect(m_workspaceController->filePreviewViewModel(), &FilePreviewViewModel::previewReady, this,
            [this](const FilePreview& preview) {
                if (!m_pendingContentSearchLineJump)
                {
                    return;
                }
                const auto& [path, oneBasedLine] = *m_pendingContentSearchLineJump;
                if (preview.kind() != FilePreviewKind::Text || preview.path() != path)
                {
                    return;
                }
                m_previewPanelWidget->goToLine(path, oneBasedLine);
                m_pendingContentSearchLineJump.reset();
            });
    // previewLoading() only fires when the resolved target actually changes (not for a
    // directory-watch refresh of the same target -- see FilePreviewViewModel's own comment), and
    // onContentSearchLineActivated sets the pending jump only after the navigation that triggers
    // its own previewLoading() has already fired. So any previewLoading() tick seen while a jump is
    // still pending is necessarily from a further, different navigation that supersedes it.
    connect(m_workspaceController->filePreviewViewModel(), &FilePreviewViewModel::previewLoading, this,
            [this]() { m_pendingContentSearchLineJump.reset(); });

    // Preview's content-search highlight layer (Architecture.md §14.105): browsing to a different
    // text file while content search stays active must also get highlighted. Relies on the same
    // direct-connection ordering as the line-jump lambda above -- PreviewPanelWidget's own
    // onPreviewReady handler (connected in its ctor, which runs before this one) has already loaded
    // the new text by the time this fires.
    connect(m_workspaceController->filePreviewViewModel(), &FilePreviewViewModel::previewReady, this,
            [this](const FilePreview&) { refreshPreviewContentSearchHighlight(); });

    m_monitoringPanelWidget =
        new MonitoringPanelWidget(m_workspaceController->monitoringPanelViewModel(), m_uiThemeChangeNotifier, m_rightPanelWidget);
    m_monitoringPanelWidget->setObjectName(QStringLiteral("monitoringPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Monitoring"), m_monitoringPanelWidget, QStringLiteral("rightPanelMonitoringButton"));
    connect(m_monitoringPanelWidget, &MonitoringPanelWidget::monitoringFailed, this, &MainWindow::onStatusMessage);

    // Media Viewer mode's "Searcher" tab (Architecture.md §14.61): registered up front like the
    // three tabs above, but its header button starts hidden -- it only appears while the mode is
    // active (MediaViewerModeController::onModeActiveChanged), toggled via
    // SidePanelWidget::setPanelTabButtonVisible.
    m_mediaViewerSearchPanelWidget = new MediaViewerSearchPanelWidget(m_uiThemeChangeNotifier, m_rightPanelWidget);
    m_mediaViewerSearchPanelWidget->setObjectName(QStringLiteral("mediaViewerSearchPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Searcher"), m_mediaViewerSearchPanelWidget, QStringLiteral("rightPanelSearcherButton"));
    m_rightPanelWidget->setPanelTabButtonVisible(3, false);

    // Left panel (Architecture.md §14.41/§14.42): same side-panel mechanics, mirrored, with "Tree"
    // and "Search" tabs.
    m_leftPanelWidget = new SidePanelWidget(SidePanelWidget::Edge::Left, QStringLiteral("leftPanel"), m_uiThemeChangeNotifier, this);
    createFolderTree();
    m_leftPanelWidget->addPanelTab(tr("Tree"), m_folderTreeView, QStringLiteral("leftPanelTreeButton"));
    createSearchPanel();
    m_leftPanelWidget->addPanelTab(tr("Search"), m_searchPanelWidget, QStringLiteral("leftPanelSearchButton"));
    createContentSearchPanel();
    m_leftPanelWidget->addPanelTab(tr("Find in Files"), m_contentSearchPanelWidget, QStringLiteral("leftPanelContentSearchButton"));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    splitter->addWidget(m_leftPanelWidget);
    splitter->addWidget(m_workspaceLayoutWidget);
    splitter->addWidget(m_rightPanelWidget);
    splitter->setCollapsible(1, false);
    splitter->setSizes({ m_leftPanelWidget->collapsedWidth(), 1000, m_rightPanelWidget->collapsedWidth() });
    // Without explicit stretch factors, QSplitter distributes any width change (e.g. the window
    // being wider than the sizes set above, which happens on every startup since restoreGeometry()
    // runs before this splitter exists) proportionally across all three panes, inflating the side
    // panels' width too. Pinning the side panels to 0 and the workspace to 1 makes the workspace
    // alone absorb all such changes, so a side panel's width only ever changes via resizeSidePanel()
    // above -- otherwise the inflated width gets persisted on close and compounds on every launch.
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    setCentralWidget(splitter);

    // Expanding/collapsing one side panel trades width with the workspace only; the other side
    // panel keeps whatever width it currently has.
    const auto resizeSidePanel = [splitter](int sideIndex, int sideWidth) {
        QList<int> sizes = splitter->sizes();
        const int total = std::max(splitter->width(), splitter->sizeHint().width());
        const int otherSideWidth = sizes[2 - sideIndex];
        sizes[sideIndex] = sideWidth;
        sizes[1] = std::max(0, total - sideWidth - otherSideWidth);
        splitter->setSizes(sizes);
    };
    connect(m_leftPanelWidget, &SidePanelWidget::expansionChanged, splitter,
            [resizeSidePanel, leftPanel = m_leftPanelWidget](bool expanded) {
                resizeSidePanel(0, expanded ? leftPanel->preferredExpandedWidth() : leftPanel->collapsedWidth());
            });
    connect(m_rightPanelWidget, &SidePanelWidget::expansionChanged, splitter,
            [resizeSidePanel, rightPanel = m_rightPanelWidget](bool expanded) {
                resizeSidePanel(2, expanded ? rightPanel->preferredExpandedWidth() : rightPanel->collapsedWidth());
            });

    // Restore left/right side-panel expand/tab/width state (Architecture.md §14.14, §14.25/§14.41)
    // -- activateTab() goes through the same logic as a real tab-button click, including emitting
    // expansionChanged(true), which the resizeSidePanel lambdas above pick up to resize the
    // splitter using the width just set here.
    constexpr int kLeftPanelTabCount = 3;
    constexpr int kRightPanelTabCount = 4;
    if (m_initialLeftPanelState.width > 0)
    {
        m_leftPanelWidget->setPreferredExpandedWidth(m_initialLeftPanelState.width);
    }
    if (m_initialLeftPanelState.expanded)
    {
        const int tabIndex = std::clamp(m_initialLeftPanelState.activeTabIndex, 0, kLeftPanelTabCount - 1);
        m_leftPanelWidget->activateTab(tabIndex);
    }
    if (m_initialRightPanelState.width > 0)
    {
        m_rightPanelWidget->setPreferredExpandedWidth(m_initialRightPanelState.width);
    }
    if (m_initialRightPanelState.expanded)
    {
        // Clamped to exclude the Searcher slot (index 3): Media Viewer mode is never persisted
        // (Architecture.md §14.61), so no tab can have it active immediately on launch, and
        // restoring directly onto the Searcher tab would show it with no mode actually active.
        const int tabIndex = std::clamp(m_initialRightPanelState.activeTabIndex, 0, kRightPanelTabCount - 2);
        m_rightPanelWidget->activateTab(tabIndex);
    }

    for (WorkspacePaneId id : kAllPanes)
    {
        connect(m_workspaceLayoutWidget->paneWidget(id), &WorkspacePaneWidget::navigationFailed, this, &MainWindow::onStatusMessage);
        connect(m_workspaceLayoutWidget->paneWidget(id), &WorkspacePaneWidget::gitOperationFailed, this, &MainWindow::onStatusMessage);
        connect(m_workspaceLayoutWidget->paneWidget(id), &WorkspacePaneWidget::bulkRenameRequested, this,
                &MainWindow::onBulkRenameRequested);
    }

    connect(m_workspaceController->fileOperationsController(), &FileOperationsController::operationFailed, this,
            &MainWindow::onStatusMessage);
    connect(m_workspaceController->fileOperationsController(), &FileOperationsController::insufficientDiskSpace, this,
            [this](const QString& message) { QMessageBox::warning(this, tr("Not Enough Space"), message); });

    // Constructed last in this method: it wires directly to m_workspaceLayoutWidget, both side
    // panels, and m_mediaViewerSearchPanelWidget above, all of which must already exist
    // (Architecture.md §14.96). m_mediaViewerModeAction itself was created earlier
    // (createMediaViewerModeAction()), before createMenuBar() needed it.
    m_mediaViewerModeController =
        new MediaViewerModeController(*m_workspaceController, *m_workspaceLayoutWidget, *m_leftPanelWidget,
                                       *m_rightPanelWidget, *m_mediaViewerSearchPanelWidget, *m_layoutActionGroup,
                                       m_mediaViewerModeAction, this);
    connect(m_mediaViewerModeController, &MediaViewerModeController::statusMessage, this, &MainWindow::onStatusMessage);
}

void MainWindow::createFolderTree()
{
    FileOperationsController* fileOperations = m_workspaceController->fileOperationsController();

    m_folderTreeView = new FolderTreeView(m_workspaceController->folderTreeModel(), m_leftPanelWidget);
    m_folderTreeView->setObjectName(QStringLiteral("folderTreeView"));

    connect(m_folderTreeView, &FolderTreeView::navigateRequested, this, &MainWindow::navigateFocusedPaneTo);
    connect(m_folderTreeView, &FolderTreeView::renameRequested, fileOperations, &FileOperationsController::renamePath);
    connect(m_folderTreeView, &FolderTreeView::deleteRequested, this,
            [this, fileOperations](const std::filesystem::path& path, bool permanent) {
                FileItemContextMenu::confirmAndDelete(this, *fileOperations, { path }, permanent);
            });
    connect(m_folderTreeView, &FolderTreeView::itemContextMenuRequested, this,
            [this, fileOperations](const std::filesystem::path& path, bool isTopLevelLocation, const QPoint& globalPos) {
                FileItemContextMenu::Callbacks callbacks;
                if (!isTopLevelLocation)
                {
                    callbacks.rename = [this, path]() { m_folderTreeView->beginRename(path); };
                    callbacks.deleteItems = [this, fileOperations, path]() {
                        FileItemContextMenu::confirmAndDelete(this, *fileOperations, { path }, false);
                    };
                }
                callbacks.reportError = [this](const QString& message) { onStatusMessage(message); };
                // "Open" on a folder in the tree means "show it here", not "hand it to the shell".
                callbacks.open = [this](const std::filesystem::path& target) { navigateFocusedPaneTo(target); };

                FileItemContextMenu::show(this, *fileOperations, { path }, false, callbacks, globalPos);
            });
}

void MainWindow::createSearchPanel()
{
    m_searchPanelWidget = new SearchPanelWidget(m_fileDecorationsViewModel.decorationRules(), m_fileDecorationsViewModel,
                                                 m_uiThemeChangeNotifier, m_hotkeyManager, m_leftPanelWidget);

    connect(m_searchPanelWidget, &SearchPanelWidget::queryChanged, this, [this](const QString& query) {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->updateSearchQuery(query);
        }
    });
    connect(m_searchPanelWidget, &SearchPanelWidget::refreshRequested, this, [this](const QString& query) {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->refreshSearch(query);
        }
    });
    connect(m_searchPanelWidget, &SearchPanelWidget::clearRequested, this, [this]() {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->quickSearch()->exit();
        }
    });
    connect(m_searchPanelWidget, &SearchPanelWidget::advancedSearchRequested, this, [this]() {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->showAdvancedSearchPanel();
        }
    });
    connect(m_searchPanelWidget, &SearchPanelWidget::searchOptionsChanged, this, [this](const NameSearchOptions& options) {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->quickSearch()->setOptions(options, tab->currentPath());
        }
    });
    connect(m_searchPanelWidget, &SearchPanelWidget::resultActivated, this, &MainWindow::onSearchResultActivated);
    connect(m_searchPanelWidget, &SearchPanelWidget::selectionChanged, this, [this](const std::vector<FileNode>& entries) {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->setSelectedEntries(entries);
        }
    });
}

void MainWindow::createContentSearchPanel()
{
    m_contentSearchPanelWidget = new ContentSearchPanelWidget(m_leftPanelWidget);

    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::queryChanged, this, [this](const QString& query) {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->contentSearch()->updateQuery(query, tab->currentPath());
        }
    });
    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::refreshRequested, this, [this](const QString& query) {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->contentSearch()->refresh(query, tab->currentPath());
        }
    });
    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::clearRequested, this, [this]() {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->contentSearch()->exit();
        }
    });
    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::searchOptionsChanged, this,
            [this](const ContentSearchOptions& options) {
                if (TabViewModel* tab = m_workspaceController->focusedTab())
                {
                    tab->contentSearch()->setOptions(options, tab->currentPath());
                }
            });
    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::resultActivated, this, &MainWindow::onContentSearchResultActivated);
    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::lineActivated, this, &MainWindow::onContentSearchLineActivated);
    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::linePreviewRequested, this,
            &MainWindow::onContentSearchLinePreviewRequested);
    connect(m_contentSearchPanelWidget, &ContentSearchPanelWidget::selectionChanged, this,
            [this](const std::vector<FileNode>& entries) {
                if (TabViewModel* tab = m_workspaceController->focusedTab())
                {
                    tab->setSelectedEntries(entries);
                }
            });
}

void MainWindow::onSearchResultActivated(const std::filesystem::path& path, bool isDirectory)
{
    WorkspacePaneWidget* paneWidget = m_workspaceLayoutWidget->paneWidget(m_workspaceController->focusedPane());
    if (!paneWidget)
    {
        return;
    }

    if (isDirectory)
    {
        paneWidget->navigateAndSelect(path, std::nullopt);
    }
    else
    {
        paneWidget->navigateAndSelect(path.parent_path(), path);
    }
}

void MainWindow::onContentSearchResultActivated(const std::filesystem::path& path, bool isDirectory)
{
    onSearchResultActivated(path, isDirectory);
}

void MainWindow::onContentSearchLineActivated(const std::filesystem::path& path, int oneBasedLine)
{
    onContentSearchResultActivated(path, /*isDirectory=*/false);

    // A double-click is physically a single click followed by a second one: the leading click
    // already selected this file, which may have already driven FilePreviewViewModel through a
    // synchronous previewLoading()/previewReady() cycle before this handler ever runs. If so, the
    // navigateAndSelect() above resolves to a FileNode equal to the one already selected and
    // TabViewModel::setSelectedEntries()'s dedup guard swallows it -- no further previewReady()
    // will ever arrive for `path`, so a pending jump would dangle forever. Try the jump immediately
    // against whatever the preview panel is already showing; only fall back to the pending-jump
    // path (consumed by the previewReady() connection in createWorkspace()) when the preview is
    // still catching up to a genuinely new navigation.
    if (m_previewPanelWidget->goToLine(path, oneBasedLine))
    {
        m_pendingContentSearchLineJump.reset();
        return;
    }

    m_pendingContentSearchLineJump = std::make_pair(path, oneBasedLine);
}

void MainWindow::onContentSearchLinePreviewRequested(const std::filesystem::path& path, int oneBasedLine)
{
    if (m_previewPanelWidget->goToLine(path, oneBasedLine))
    {
        m_pendingContentSearchLineJump.reset();
        return;
    }

    m_pendingContentSearchLineJump = std::make_pair(path, oneBasedLine);
}

void MainWindow::navigateFocusedPaneTo(const std::filesystem::path& path)
{
    TabViewModel* tab = m_workspaceController->focusedTab();
    WorkspacePaneWidget* paneWidget = m_workspaceLayoutWidget->paneWidget(m_workspaceController->focusedPane());
    if (!tab || !paneWidget)
    {
        return;
    }

    // Re-clicking the folder the tab is already showing would otherwise push a duplicate history
    // entry.
    if (!pathsEqualCaseInsensitive(tab->currentPath(), path))
    {
        paneWidget->navigateAndSelect(path, std::nullopt);
    }

    // Snap the tree back to wherever the tab actually is: a no-op after a successful navigation,
    // but it undoes the click's selection if navigation failed or the user declined leaving a
    // monitored folder.
    m_folderTreeView->revealPath(m_workspaceController->folderTreeModel()->activePath());
}

void MainWindow::createMainStatusBar()
{
    m_mainStatusBarWidget = new MainStatusBarWidget(this);
    m_mainStatusBarWidget->setObjectName(QStringLiteral("mainStatusBarWidget"));
    statusBar()->addPermanentWidget(m_mainStatusBarWidget, 1);
}

void MainWindow::createHotkeyWindowDispatcher()
{
    m_hotkeyWindowDispatcher = new HotkeyWindowDispatcher(m_hotkeyManager, this);
    qApp->installEventFilter(m_hotkeyWindowDispatcher);

    // Resolved at invocation time (not here), since which pane is "focused" changes at runtime --
    // avoids re-registering callbacks on every WorkspaceController::focusedPaneChanged.
    auto focusedPaneWidget = [this]() -> WorkspacePaneWidget* {
        return m_workspaceLayoutWidget->paneWidget(m_workspaceController->focusedPane());
    };

    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::NewTab, [focusedPaneWidget]() {
        if (WorkspacePaneWidget* pane = focusedPaneWidget())
        {
            pane->activateNewTab();
        }
    });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::CloseTab, [focusedPaneWidget]() {
        if (WorkspacePaneWidget* pane = focusedPaneWidget())
        {
            pane->closeCurrentTab();
        }
    });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::NextTab, [focusedPaneWidget]() {
        if (WorkspacePaneWidget* pane = focusedPaneWidget())
        {
            pane->activateNextTab();
        }
    });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::PreviousTab, [focusedPaneWidget]() {
        if (WorkspacePaneWidget* pane = focusedPaneWidget())
        {
            pane->activatePreviousTab();
        }
    });

    // Panes & Layout (spec §3.3, Milestone 4). Focus-Pane-A/B/C/D: resolved at invocation time
    // (not captured once), same reasoning as focusedPaneWidget above. Guarded against focusing a
    // currently-hidden pane (e.g. Pane D under SplitLayout::Single) via
    // WorkspaceLayoutTopology::visiblePanes -- focusing an invisible pane makes no sense, so this
    // is a deliberate no-op rather than, say, switching layout to make it visible first.
    //
    // Every one of these calls WorkspacePaneWidget::focusBrowserView() -- never
    // WorkspaceController::setFocusedPane() directly -- since setFocusedPane is only ever a
    // consequence of real Qt focus movement (WorkspaceLayoutWidget's QApplication::focusChanged
    // connection), not a way to cause it; see plan.md's M4 notes.
    auto focusPaneIfVisible = [this](WorkspacePaneId id) {
        const std::vector<WorkspacePaneId> visible = WorkspaceLayoutTopology::visiblePanes(m_workspaceController->layout());
        if (std::find(visible.begin(), visible.end(), id) == visible.end())
        {
            return;
        }
        if (WorkspacePaneWidget* pane = m_workspaceLayoutWidget->paneWidget(id))
        {
            pane->focusBrowserView();
        }
    };

    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::FocusPaneA,
                                                [focusPaneIfVisible]() { focusPaneIfVisible(WorkspacePaneId::PaneA); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::FocusPaneB,
                                                [focusPaneIfVisible]() { focusPaneIfVisible(WorkspacePaneId::PaneB); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::FocusPaneC,
                                                [focusPaneIfVisible]() { focusPaneIfVisible(WorkspacePaneId::PaneC); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::FocusPaneD,
                                                [focusPaneIfVisible]() { focusPaneIfVisible(WorkspacePaneId::PaneD); });

    // Cycle focus to next/previous pane: index math factored into
    // WorkspaceLayoutTopology::cyclePane (pure, unit-tested) so it's shared with its test rather
    // than duplicated here. nullopt covers both "fewer than 2 panes visible" (e.g. Single layout --
    // nothing to cycle to) and "focused pane isn't even in the visible set" (shouldn't happen, but
    // a safe no-op either way). Same "cause real focus, let setFocusedPane follow" rule as above.
    auto cyclePane = [this](int step) {
        const std::optional<WorkspacePaneId> next =
            WorkspaceLayoutTopology::cyclePane(m_workspaceController->layout(), m_workspaceController->focusedPane(), step);
        if (!next)
        {
            return;
        }
        if (WorkspacePaneWidget* pane = m_workspaceLayoutWidget->paneWidget(*next))
        {
            pane->focusBrowserView();
        }
    };

    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::CycleNextPane, [cyclePane]() { cyclePane(1); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::CyclePreviousPane, [cyclePane]() { cyclePane(-1); });

    // Layout: single/2V/2H/4-grid. Reuses the exact same m_workspaceController->setLayout(layout)
    // call createLayoutActions() already wired to each QAction::triggered -- the dispatcher is a
    // second trigger path onto the same code, never a QAction::setShortcut call, keeping the
    // shared design's "no Qt-native shortcuts" rule consistent even for these pre-existing actions.
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::LayoutSingle,
                                                [this]() { m_workspaceController->setLayout(SplitLayout::Single); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::LayoutTwoVertical,
                                                [this]() { m_workspaceController->setLayout(SplitLayout::TwoVertical); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::LayoutTwoHorizontal,
                                                [this]() { m_workspaceController->setLayout(SplitLayout::TwoHorizontal); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::LayoutFourGrid,
                                                [this]() { m_workspaceController->setLayout(SplitLayout::FourGrid); });

    // Panels (spec §3.4, Milestone 5). Toggle left/right panel act directly on MainWindow's
    // long-lived m_leftPanelWidget/m_rightPanelWidget singletons; toggle bottom panel resolves the
    // currently-focused pane (same focusedPaneWidget lambda as the Tabs callbacks above) since the
    // bottom panel is a per-tab widget, not a MainWindow-owned singleton (see
    // WorkspacePaneWidget::toggleBottomPanel()).
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ToggleLeftPanel, [this]() { m_leftPanelWidget->toggleExpanded(); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ToggleRightPanel, [this]() { m_rightPanelWidget->toggleExpanded(); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ToggleBottomPanel, [focusedPaneWidget]() {
        if (WorkspacePaneWidget* pane = focusedPaneWidget())
        {
            pane->toggleBottomPanel();
        }
    });

    // Show left/right panel's X tab: activateTab() already expands-if-collapsed and selects the
    // given index (same method MainWindow's own startup-state-restore code calls, see
    // kLeftPanelTabCount/kRightPanelTabCount above) -- left panel 0=Tree/1=Search/2=Find in Files,
    // right panel 0=Tags/1=Preview (verified against the addPanelTab() call order above).
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ShowLeftTreeTab, [this]() { m_leftPanelWidget->activateTab(0); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ShowLeftSearchTab, [this]() { m_leftPanelWidget->activateTab(1); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ShowLeftFindInFilesTab,
                                                [this]() { m_leftPanelWidget->activateTab(2); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ShowRightTagsTab, [this]() { m_rightPanelWidget->activateTab(0); });
    m_hotkeyWindowDispatcher->registerCallback(HotkeyAction::ShowRightPreviewTab, [this]() { m_rightPanelWidget->activateTab(1); });
}

void MainWindow::showTagManagerDialog()
{
    TagManagerViewModel viewModel(m_tagManagementUseCase, m_tagChangeNotifier, m_advancedSearchService,
                                  m_iconLoadingService, m_fileTagsService);
    TagManagerDialog dialog(&viewModel, m_fileDecorationsViewModel.decorationRules(), m_fileDecorationsViewModel,
                             m_hotkeyManager, this);
    dialog.exec();
}

void MainWindow::showManageFavoritesDialog()
{
    ManageFavoritesDialog dialog(&m_favoritesViewModel, this);
    dialog.exec();
}

void MainWindow::rebuildWorkspacesMenuItems()
{
    m_workspacesMenu->clear();

    for (const WorkspaceEntry& entry : m_workspaceStore.listWorkspaces())
    {
        QAction* action = m_workspacesMenu->addAction(entry.name);
        action->setCheckable(true);
        action->setChecked(entry.id == m_launchContext.workspaceId);
        connect(action, &QAction::triggered, this, [this, entry]() { launchOrActivateWorkspace(entry); });
    }
}

void MainWindow::launchOrActivateWorkspace(const WorkspaceEntry& entry)
{
    if (entry.id == m_launchContext.workspaceId)
    {
        raise();
        activateWindow();
        return;
    }

    // If `entry` is already open in another window, that process's WorkspaceInstanceGuard already
    // owns its name -- the process started here simply loses that race, forwards activation to the
    // real owner, and exits before constructing CompositionRoot/MainWindow (Architecture.md §14.60).
    // No pre-check is done here first; a pre-check would itself race against the same condition.
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                             { QStringLiteral("--data-dir"), QString::fromStdWString(m_launchContext.appDataDir.wstring()),
                               QStringLiteral("--workspace"), entry.id });
}

void MainWindow::onSaveAsWorkspaceTriggered()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Save as Workspace"), tr("Workspace name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
    {
        return;
    }
    const QString trimmedName = name.trimmed();

    const std::vector<WorkspaceEntry> existing = m_workspaceStore.listWorkspaces();
    const auto it = std::find_if(existing.begin(), existing.end(),
                                  [&trimmedName](const WorkspaceEntry& entry) { return entry.name == trimmedName; });

    QString targetId;
    if (it != existing.end())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Save as Workspace"), tr("A workspace named \"%1\" already exists. Overwrite it?").arg(trimmedName));
        if (answer != QMessageBox::Yes)
        {
            return;
        }
        targetId = it->id;
    }
    else
    {
        targetId = m_workspaceStore.createWorkspace(trimmedName);
    }

    // Deliberately does not rebind this window to targetId -- it stays tied to whatever workspace
    // it was launched with, and keeps saving back there on close (Architecture.md §14.60).
    const AppConfigStore targetStore(m_workspaceStore.configFilePath(targetId));
    targetStore.save(captureCurrentConfig());
}

void MainWindow::showWorkspaceManagerDialog()
{
    WorkspaceManagerDialog dialog(m_workspaceStore, m_launchContext, this);
    dialog.exec();
}

QString MainWindow::currentWorkspaceName() const
{
    for (const WorkspaceEntry& entry : m_workspaceStore.listWorkspaces())
    {
        if (entry.id == m_launchContext.workspaceId)
        {
            return entry.name;
        }
    }
    return m_launchContext.workspaceId;
}

void MainWindow::showEnvironmentVariablesDialog()
{
    EnvironmentVariablesDialog dialog(&m_environmentVariablesViewModel, this);
    dialog.exec();
}

void MainWindow::showTextEditorDialog()
{
    TextEditorOptionsDialog optionsDialog(this);
    if (optionsDialog.exec() != QDialog::Accepted)
        return;

    TextEditorDialog dialog(optionsDialog.options(), this, &m_syntaxHighlightEngine, &m_highlightThemeViewModel);
    dialog.exec();
}

void MainWindow::showDuplicateFinderDialog()
{
    if (m_duplicateFinderDialog == nullptr)
    {
        TabViewModel* tab = m_workspaceController->focusedTab();
        const std::filesystem::path initialPath = tab != nullptr ? tab->currentPath() : std::filesystem::path();

        m_duplicateFinderDialog =
            new DuplicateFinderDialog(m_duplicateFinderService, m_workspaceController->fileNavigationUseCase(),
                                       m_notificationCenter, initialPath, this);
        connect(m_duplicateFinderDialog, &DuplicateFinderDialog::openContainingFolderRequested, this,
                [this](const std::filesystem::path& path) { onSearchResultActivated(path, false); });
    }

    m_duplicateFinderDialog->show();
    m_duplicateFinderDialog->raise();
    m_duplicateFinderDialog->activateWindow();
}

void MainWindow::showBulkRenameDialogFromMenu()
{
    TabViewModel* tab = m_workspaceController->focusedTab();
    if (!tab)
    {
        onStatusMessage(tr("No folder is open."));
        return;
    }

    if (tab->selectedEntries().size() >= 2)
    {
        showBulkRenameDialogFor(tab->selectedEntries());
        return;
    }

    auto result = m_workspaceController->fileNavigationUseCase().listDirectory(tab->currentPath());
    if (result.hasError() || result.value().empty())
    {
        onStatusMessage(tr("This folder has nothing to rename."));
        return;
    }

    showBulkRenameDialogFor(result.value());
}

void MainWindow::showBulkRenameDialogFor(const std::vector<FileNode>& selection)
{
    if (selection.empty())
    {
        return;
    }

    BulkRenameViewModel viewModel(m_bulkRenameUseCase, *m_workspaceController->fileOperationsController(), selection, this);
    BulkRenameDialog dialog(&viewModel, this);
    dialog.exec();
}

void MainWindow::onBulkRenameRequested(TabViewModel* tab, const std::vector<std::filesystem::path>& paths)
{
    // `paths` is already exactly tab->selectedEntries() in path form (WorkspacePaneWidget built it
    // from that same list) -- resolving back through selectedEntries() avoids a redundant stat() per
    // path.
    Q_UNUSED(paths);
    if (!tab)
    {
        return;
    }

    showBulkRenameDialogFor(tab->selectedEntries());
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(m_syntaxHighlightEngine, m_highlightThemeViewModel, m_fileDecorationsViewModel,
                          m_notificationSettingsViewModel, m_columnVisibilitySettingsViewModel, m_hotkeyManager,
                          m_uiThemeChangeNotifier, this);

    connect(&dialog, &SettingsDialog::showTabCloseButtonsChanged, this, [this](bool visible) {
        for (WorkspacePaneId id : kAllPanes)
        {
            m_workspaceLayoutWidget->paneWidget(id)->setTabCloseButtonsVisible(visible);
        }
    });

    dialog.exec();
}

void MainWindow::onLayoutChanged(SplitLayout layout)
{
    const auto it = std::find(kLayouts.begin(), kLayouts.end(), layout);
    if (it == kLayouts.end())
    {
        return;
    }

    const auto index = std::distance(kLayouts.begin(), it);
    m_layoutActions[static_cast<int>(index)]->setChecked(true);
}

void MainWindow::onStatusMessage(const QString& message)
{
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onFocusedTabChanged(TabViewModel* tab)
{
    if (m_sortTrackedTab)
    {
        disconnect(m_sortTrackedTab, &TabViewModel::sortOrderChanged, this, &MainWindow::onSortOrderChanged);
    }
    m_sortTrackedTab = tab;

    if (m_searchTrackedTab)
    {
        disconnect(m_searchTrackedTab->quickSearch(), &TabQuickSearchViewModel::failed, this, &MainWindow::onStatusMessage);
        disconnect(m_searchTrackedTab->advancedSearch(), &TabAdvancedSearchViewModel::failed, this, &MainWindow::onStatusMessage);
        // Every resultsChanged/modeChanged/loading/loadingFinished/failed connection into
        // m_searchPanelWidget below is a lambda (receiver == m_searchPanelWidget, never `this`) --
        // disconnecting all of them by receiver alone is safe and precise, same pattern as
        // MonitoringPanelViewModel::disconnectDisplayedTab.
        disconnect(m_searchTrackedTab->quickSearch(), nullptr, m_searchPanelWidget, nullptr);
    }
    m_searchTrackedTab = tab;

    if (m_contentSearchTrackedTab)
    {
        disconnect(m_contentSearchTrackedTab->contentSearch(), &TabContentSearchViewModel::failed, this, &MainWindow::onStatusMessage);
        // Same by-receiver disconnect posture as m_searchTrackedTab's block just above.
        disconnect(m_contentSearchTrackedTab->contentSearch(), nullptr, m_contentSearchPanelWidget, nullptr);
        // Preview's content-search highlight layer (Architecture.md §14.105): its own
        // resultsChanged/modeChanged connections below, torn down the same by-receiver way.
        disconnect(m_contentSearchTrackedTab->contentSearch(), nullptr, m_previewPanelWidget, nullptr);
    }
    m_contentSearchTrackedTab = tab;
    refreshPreviewContentSearchHighlight();

    // Media Viewer mode's tracked-tab dispatch and enabled state live entirely in
    // MediaViewerModeController now (Architecture.md §14.96) -- it handles both branches below
    // (tab == nullptr included) internally.
    m_mediaViewerModeController->setFocusedTab(tab);

    if (m_driveInfoTrackedTab)
    {
        disconnect(m_driveInfoTrackedTab, &TabViewModel::currentPathChanged, this, &MainWindow::onActiveTabPathChanged);
    }
    m_driveInfoTrackedTab = tab;

    if (!tab)
    {
        m_sortByMenu->setEnabled(false);
        m_searchPanelWidget->clearForNoTab();
        m_contentSearchPanelWidget->clearForNoTab();
        onActiveTabPathChanged({});
        m_favoritesMenuController->setFocusedTab(nullptr);
        return;
    }

    m_sortByMenu->setEnabled(true);
    connect(tab, &TabViewModel::sortOrderChanged, this, &MainWindow::onSortOrderChanged);
    onSortOrderChanged(tab->sortCriterion(), tab->sortAscending());

    TabQuickSearchViewModel* quickSearch = tab->quickSearch();
    connect(quickSearch, &TabQuickSearchViewModel::failed, this, &MainWindow::onStatusMessage);
    connect(tab->advancedSearch(), &TabAdvancedSearchViewModel::failed, this, &MainWindow::onStatusMessage);

    connect(quickSearch, &TabQuickSearchViewModel::resultsChanged, m_searchPanelWidget, [this, quickSearch](const std::vector<FileNode>& entries) {
        m_searchPanelWidget->setResults(entries, quickSearch->resultsModel()->currentDirectory(), quickSearch->resultIcons());
    });
    // resultsChanged alone misses icons that resolve after the candidate list itself last changed
    // (the common case -- IconLoadingService's batches arrive asynchronously, well after
    // applyFilter already emitted) -- see TabQuickSearchViewModel::resultIconsChanged.
    connect(quickSearch, &TabQuickSearchViewModel::resultIconsChanged, m_searchPanelWidget, [this, quickSearch]() {
        m_searchPanelWidget->applyIcons(quickSearch->resultIcons());
    });
    connect(quickSearch, &TabQuickSearchViewModel::modeChanged, m_searchPanelWidget, [this](bool active) {
        if (!active)
        {
            m_searchPanelWidget->setResults({}, {});
        }
    });
    connect(quickSearch, &TabQuickSearchViewModel::loading, m_searchPanelWidget, [this]() { m_searchPanelWidget->setLoading(true); });
    connect(quickSearch, &TabQuickSearchViewModel::loadingFinished, m_searchPanelWidget, [this]() { m_searchPanelWidget->setLoading(false); });
    connect(quickSearch, &TabQuickSearchViewModel::failed, m_searchPanelWidget, [this]() { m_searchPanelWidget->setLoading(false); });

    m_searchPanelWidget->setQuery(quickSearch->isActive() ? quickSearch->query() : QString());
    m_searchPanelWidget->setOptions(quickSearch->options());
    if (quickSearch->isActive())
    {
        m_searchPanelWidget->setResults(quickSearch->resultsModel()->entries(), quickSearch->resultsModel()->currentDirectory(),
                                         quickSearch->resultIcons());
    }
    else
    {
        m_searchPanelWidget->setResults({}, {});
    }

    TabContentSearchViewModel* contentSearch = tab->contentSearch();
    connect(contentSearch, &TabContentSearchViewModel::failed, this, &MainWindow::onStatusMessage);

    connect(contentSearch, &TabContentSearchViewModel::resultsChanged, m_contentSearchPanelWidget,
            [this, contentSearch](const std::vector<FileContentMatch>& matches) {
                m_contentSearchPanelWidget->setResults(matches, contentSearch->root());
            });
    connect(contentSearch, &TabContentSearchViewModel::modeChanged, m_contentSearchPanelWidget, [this](bool active) {
        if (!active)
        {
            m_contentSearchPanelWidget->setResults({}, {});
        }
    });
    connect(contentSearch, &TabContentSearchViewModel::loading, m_contentSearchPanelWidget,
            [this]() { m_contentSearchPanelWidget->setLoading(true); });
    connect(contentSearch, &TabContentSearchViewModel::loadingFinished, m_contentSearchPanelWidget,
            [this]() { m_contentSearchPanelWidget->setLoading(false); });
    connect(contentSearch, &TabContentSearchViewModel::failed, m_contentSearchPanelWidget,
            [this]() { m_contentSearchPanelWidget->setLoading(false); });

    // Preview's content-search highlight layer (Architecture.md §14.105): every settled query edit
    // and every Match-Case/Whole-Word/Regex toggle re-fires resultsChanged/modeChanged (see
    // TabContentSearchViewModel's own class comment), so re-pushing the highlight on both keeps it
    // live-tracking the search state without opening/activating the Find bar.
    connect(contentSearch, &TabContentSearchViewModel::resultsChanged, m_previewPanelWidget,
            [this]() { refreshPreviewContentSearchHighlight(); });
    connect(contentSearch, &TabContentSearchViewModel::modeChanged, m_previewPanelWidget,
            [this]() { refreshPreviewContentSearchHighlight(); });

    m_contentSearchPanelWidget->setQuery(contentSearch->isActive() ? contentSearch->query() : QString());
    m_contentSearchPanelWidget->setOptions(contentSearch->options());
    if (contentSearch->isActive())
    {
        m_contentSearchPanelWidget->setResults(contentSearch->matches(), contentSearch->root());
    }
    else
    {
        m_contentSearchPanelWidget->setResults({}, {});
    }

    connect(tab, &TabViewModel::currentPathChanged, this, &MainWindow::onActiveTabPathChanged);
    onActiveTabPathChanged(tab->currentPath());

    m_favoritesMenuController->setFocusedTab(tab);
}

void MainWindow::refreshPreviewContentSearchHighlight()
{
    if (!m_contentSearchTrackedTab || !m_contentSearchTrackedTab->contentSearch()->isActive())
    {
        m_previewPanelWidget->setContentSearchHighlight(QString(), {});
        return;
    }

    TabContentSearchViewModel* contentSearch = m_contentSearchTrackedTab->contentSearch();
    m_previewPanelWidget->setContentSearchHighlight(contentSearch->query(), contentSearch->options().queryOptions);
}

void MainWindow::onActiveTabPathChanged(const std::filesystem::path& path)
{
    const int generation = ++m_driveInfoGeneration;

    if (!m_driveInfoTrackedTab)
    {
        m_mainStatusBarWidget->setDriveInfoText(QString());
        return;
    }

    // The actual GetVolumeInformationW/GetDiskFreeSpaceExW call runs on m_driveInfoThread, not
    // here -- CLAUDE.md's threading rule forbids disk/volume I/O on the Qt UI thread. Both hops
    // are queued functor calls (QMetaObject::invokeMethod), the same shape
    // FilePreviewViewModel::dispatchGeneration uses, since Result<DriveSpaceInfo> isn't a
    // registered Qt meta-type.
    QMetaObject::invokeMethod(
        m_driveInfoWorker,
        [this, generation, path]() {
            Result<DriveSpaceInfo> result = m_driveInfoWorker->query(path);

            QMetaObject::invokeMethod(
                this, [this, generation, result = std::move(result)]() mutable { onDriveSpaceInfoReady(generation, std::move(result)); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void MainWindow::onDriveSpaceInfoReady(int generation, Result<DriveSpaceInfo> result)
{
    if (generation != m_driveInfoGeneration)
    {
        // A newer navigation/focus change has already superseded this request -- the underlying
        // Win32 call still ran to completion, but its result is simply discarded now (mirrors
        // FilePreviewViewModel::onWorkerFinished).
        return;
    }

    if (!result)
    {
        m_mainStatusBarWidget->setDriveInfoText(QString());
        return;
    }

    const DriveSpaceInfo& info = result.value();
    const QString text = tr("%1 — %2 free of %3")
                              .arg(QString::fromStdString(info.displayLabel),
                                   QLocale().formattedDataSize(static_cast<qint64>(info.freeBytes)),
                                   QLocale().formattedDataSize(static_cast<qint64>(info.totalBytes)));
    m_mainStatusBarWidget->setDriveInfoText(text);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (m_notificationOverlayWidget)
    {
        m_notificationOverlayWidget->reposition(height() - statusBar()->height());
    }
}

void MainWindow::onSortOrderChanged(SortCriterion criterion, bool ascending)
{
    const auto it = std::find(kSortCriteria.begin(), kSortCriteria.end(), criterion);
    if (it != kSortCriteria.end())
    {
        const auto index = std::distance(kSortCriteria.begin(), it);
        m_sortCriterionActions[static_cast<int>(index)]->setChecked(true);
    }

    (ascending ? m_ascendingAction : m_descendingAction)->setChecked(true);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    bool anyMonitoringActive = false;
    for (const WorkspacePaneId paneId : kAllPanes)
    {
        WorkspacePaneViewModel* pane = m_workspaceController->pane(paneId);
        for (int i = 0; pane && i < pane->tabCount(); ++i)
        {
            if (pane->tabAt(i)->monitoring()->isActive())
            {
                anyMonitoringActive = true;
                break;
            }
        }
        if (anyMonitoringActive)
        {
            break;
        }
    }

    if (anyMonitoringActive)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Exit"), tr("Folder monitoring is active. Closing the application will stop it. Continue?"));
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
    }

    m_configStore.save(captureCurrentConfig());

    QMainWindow::closeEvent(event);
}

AppConfig MainWindow::captureCurrentConfig() const
{
    AppConfig config;
    config.windowGeometry = saveGeometry();
    config.workspace = m_workspaceController->captureConfig();

    if (WorkspacePaneWidget* focusedPaneWidget = m_workspaceLayoutWidget->paneWidget(m_workspaceController->focusedPane()))
    {
        config.detailsColumnWidths = focusedPaneWidget->currentColumnWidths();
        config.detailsColumnOrder = focusedPaneWidget->currentColumnOrder();
    }
    else
    {
        config.detailsColumnWidths = m_columnWidths;
        config.detailsColumnOrder = m_columnOrder;
    }

    config.leftPanel.expanded = m_leftPanelWidget->isExpanded();
    config.leftPanel.activeTabIndex = m_leftPanelWidget->activeTabIndex();
    config.rightPanel.expanded = m_rightPanelWidget->isExpanded();
    config.rightPanel.activeTabIndex = m_rightPanelWidget->activeTabIndex();

    if (auto* splitter = qobject_cast<QSplitter*>(centralWidget()))
    {
        const QList<int> sizes = splitter->sizes();
        if (config.leftPanel.expanded && sizes.size() > 0)
        {
            config.leftPanel.width = sizes[0];
        }
        if (config.rightPanel.expanded && sizes.size() > 2)
        {
            config.rightPanel.width = sizes[2];
        }
    }

    return config;
}
