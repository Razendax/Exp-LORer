#include "MainWindow.h"

#include <algorithm>
#include <array>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QProcess>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "AdvancedSearchService.h"
#include "AppConfigStore.h"
#include "BulkRenameDialog.h"
#include "BulkRenameUseCase.h"
#include "BulkRenameViewModel.h"
#include "DriveSpaceInfoWorker.h"
#include "DuplicateFinderDialog.h"
#include "EnvironmentVariablesDialog.h"
#include "FavoritesViewModel.h"
#include "FileBrowserView.h"
#include "FileDecorationsViewModel.h"
#include "FileItemContextMenu.h"
#include "FileOperationsController.h"
#include "FolderTreeModel.h"
#include "FolderTreeView.h"
#include "ManageFavoritesDialog.h"
#include "MainStatusBarWidget.h"
#include "MediaViewerSearchPanelWidget.h"
#include "MonitoringPanelWidget.h"
#include "NotificationOverlayWidget.h"
#include "SettingsDialog.h"
#include "TabViewModel.h"
#include "TagManagerDialog.h"
#include "TagManagerViewModel.h"
#include "TextEditorDialog.h"
#include "PreviewPanelWidget.h"
#include "PathContainment.h"
#include "SearchPanelWidget.h"
#include "SidePanelWidget.h"
#include "TagPanelWidget.h"
#include "VirtualPaths.h"
#include "WorkspaceController.h"
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

    // Same QFileIconProvider-based lookup FileListModel/MonitoringLogModel use for file/folder
    // icons, applied to a favorite's target path.
    QIcon favoriteItemIcon(const std::filesystem::path& path)
    {
        static QFileIconProvider provider;
        return provider.icon(QFileInfo(QString::fromStdWString(path.wstring())));
    }

    QIcon favoriteCategoryIcon()
    {
        static QFileIconProvider provider;
        return provider.icon(QFileIconProvider::Folder);
    }

    // Custom QWidgetAction row for a favorite item: icon + name + a second, smaller/grayed line
    // with the full path, all as ONE menu entry rather than two separate actions. Both this widget
    // and its child labels are WA_TransparentForMouseEvents so mouse clicks/moves fall through to
    // the owning QMenu untouched -- QMenu then hit-tests, highlights, triggers and closes the
    // cascade exactly as it would for a plain QAction; only the highlight repaint (menu doesn't
    // paint anything behind a widget-action row) and the highlighted-text color swap are done here,
    // driven by the menu's hovered(QAction*) signal (see addFavoriteItemActions below).
    class FavoriteMenuRowWidget : public QWidget
    {
    public:
        FavoriteMenuRowWidget(const QIcon& icon, const QString& name, const QString& path, QWidget* parent = nullptr)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);

            auto* layout = new QHBoxLayout(this);
            layout->setContentsMargins(4, 3, 16, 3);
            layout->setSpacing(6);

            auto* iconLabel = new QLabel(this);
            iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            iconLabel->setPixmap(icon.pixmap(16, 16));
            layout->addWidget(iconLabel);

            auto* textLayout = new QVBoxLayout();
            textLayout->setContentsMargins(0, 0, 0, 0);
            textLayout->setSpacing(0);

            m_nameLabel = new QLabel(name, this);
            m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            textLayout->addWidget(m_nameLabel);

            m_pathLabel = new QLabel(path, this);
            m_pathLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            QFont pathFont = m_pathLabel->font();
            pathFont.setPointSizeF(pathFont.pointSizeF() * 0.85);
            m_pathLabel->setFont(pathFont);
            QPalette pathPalette = m_pathLabel->palette();
            pathPalette.setColor(QPalette::WindowText, Qt::gray);
            m_pathLabel->setPalette(pathPalette);
            textLayout->addWidget(m_pathLabel);

            layout->addLayout(textLayout, 1);

            m_normalNamePalette = m_nameLabel->palette();
            m_normalPathPalette = m_pathLabel->palette();
        }

        void setHighlighted(bool highlighted)
        {
            if (m_highlighted == highlighted)
            {
                return;
            }
            m_highlighted = highlighted;

            if (!highlighted)
            {
                m_nameLabel->setPalette(m_normalNamePalette);
                m_pathLabel->setPalette(m_normalPathPalette);
                update();
                return;
            }

            QPalette namePalette = m_normalNamePalette;
            namePalette.setColor(QPalette::WindowText, namePalette.color(QPalette::HighlightedText));
            m_nameLabel->setPalette(namePalette);

            QPalette pathPalette = m_normalPathPalette;
            pathPalette.setColor(QPalette::WindowText, pathPalette.color(QPalette::HighlightedText));
            m_pathLabel->setPalette(pathPalette);
            update();
        }

    protected:
        // QMenu paints nothing behind a widget-action row, so the usual blue selection band has to
        // be drawn here -- otherwise hovering only swaps the text to (invisible-on-white)
        // highlighted-text color, per the bug this fixed.
        void paintEvent(QPaintEvent* event) override
        {
            if (m_highlighted)
            {
                QPainter painter(this);
                painter.fillRect(rect(), palette().color(QPalette::Highlight));
            }
            QWidget::paintEvent(event);
        }

    private:
        QLabel* m_nameLabel = nullptr;
        QLabel* m_pathLabel = nullptr;
        QPalette m_normalNamePalette;
        QPalette m_normalPathPalette;
        bool m_highlighted = false;
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
                         FileTagsService& fileTagsService, UiThemeChangeNotifier& uiThemeChangeNotifier,
                         SyntaxHighlightEngine& syntaxHighlightEngine,
                         HighlightThemeViewModel& highlightThemeViewModel, FileDecorationsViewModel& fileDecorationsViewModel,
                         FavoritesViewModel& favoritesViewModel, NotificationCenter& notificationCenter,
                         NotificationSettingsViewModel& notificationSettingsViewModel,
                         ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel, DiskUsageService& diskUsageService,
                         DuplicateFinderService& duplicateFinderService, FragmentationService& fragmentationService,
                         BulkRenameUseCase& bulkRenameUseCase, EnvironmentVariablesViewModel& environmentVariablesViewModel,
                         WorkspaceStore& workspaceStore, const WorkspaceLaunchContext& launchContext,
                         const AppConfig& initialConfig, QWidget* parent)
    : QMainWindow(parent)
    , m_workspaceController(workspaceController)
    , m_configStore(configStore)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_advancedSearchService(advancedSearchService)
    , m_iconLoadingService(iconLoadingService)
    , m_fileTagsService(fileTagsService)
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
    createContextMenuModeAction();
    createShowHiddenFilesAction();
    createMediaViewerModeAction();
    createHoverPreviewAction();
    createMenuBar();
    createLayoutToolBar();
    createWorkspace();
    createMainStatusBar();

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

void MainWindow::createContextMenuModeAction()
{
    m_extendedContextMenuAction = new QAction(tr("Show shell extensions in context menu"), this);
    m_extendedContextMenuAction->setCheckable(true);

    QSettings settings;
    m_extendedContextMenuAction->setChecked(
        settings.value(QLatin1String(WorkspacePaneWidget::kShowShellExtensionsSettingsKey), false).toBool());

    connect(m_extendedContextMenuAction, &QAction::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QLatin1String(WorkspacePaneWidget::kShowShellExtensionsSettingsKey), checked);
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
                tab->searchResultsModel()->setShowHiddenFiles(checked);
                tab->advancedSearchResultsModel()->setShowHiddenFiles(checked);
            }
        }
        m_workspaceController->folderTreeModel()->setShowHiddenFiles(checked);
    });
}

void MainWindow::createMediaViewerModeAction()
{
    m_mediaViewerModeAction = new QAction(tr("Media Viewer Mode"), this);
    m_mediaViewerModeAction->setCheckable(true);
    m_mediaViewerModeAction->setEnabled(false); // re-enabled by onFocusedTabChanged once a tab exists

    connect(m_mediaViewerModeAction, &QAction::toggled, this, [this](bool checked) {
        if (checked)
        {
            // Entering targets the currently-focused tab, same as before.
            if (TabViewModel* tab = m_workspaceController->focusedTab())
            {
                tab->enterMediaViewer();
            }
            return;
        }

        // Exiting targets the tab actually in Media Viewer mode, not focusedTab() -- in a split
        // workspace, focus may have moved to a different tab since Media Viewer mode was entered
        // (Architecture.md §14.61), and exitMediaViewer() on an unrelated tab is a silent no-op.
        if (TabViewModel* tab = mediaViewerTargetTab())
        {
            tab->exitMediaViewer();
        }
    });
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
    viewMenu->addAction(m_extendedContextMenuAction);
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
    m_favoritesMenu = menuBar()->addMenu(tr("F&avorites"));

    m_addToFavoritesAction = new QAction(tr("Add to Favorites"), this);
    connect(m_addToFavoritesAction, &QAction::triggered, this, &MainWindow::onAddToFavoritesTriggered);
    m_favoritesMenu->addAction(m_addToFavoritesAction);

    m_favoritesMenu->addAction(tr("Manage Favorites..."), this, &MainWindow::showManageFavoritesDialog);
    m_favoritesMenu->addSeparator();

    // Rebuilt lazily right before the menu is shown, rather than kept live-synced against
    // favoritesChanged() -- always current with no extra wiring here (Architecture.md §14.35).
    connect(m_favoritesMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildFavoritesMenuItems);
}

void MainWindow::createWorkspacesMenu(QMenu* fileMenu)
{
    m_workspacesMenu = fileMenu->addMenu(tr("&Workspaces"));

    // Rebuilt lazily right before the menu is shown, rather than kept live-synced -- same posture
    // as m_favoritesMenu above (Architecture.md §14.60).
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
                                                          m_uiThemeChangeNotifier, m_columnWidths, m_columnOrder, this);
    m_workspaceLayoutWidget->setObjectName(QStringLiteral("workspaceLayoutWidget"));

    // Right panel (Architecture.md §14.25): a collapsible side-panel tab strip wrapping the tag UI,
    // file preview and folder monitoring log.
    m_rightPanelWidget = new SidePanelWidget(SidePanelWidget::Edge::Right, QStringLiteral("rightPanel"), m_uiThemeChangeNotifier, this);
    m_tagPanelWidget = new TagPanelWidget(m_workspaceController->tagListViewModel(), m_uiThemeChangeNotifier, m_rightPanelWidget);
    m_tagPanelWidget->setObjectName(QStringLiteral("tagPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Tags"), m_tagPanelWidget, QStringLiteral("rightPanelTagsButton"));

    m_previewPanelWidget = new PreviewPanelWidget(m_workspaceController->filePreviewViewModel(), m_syntaxHighlightEngine,
                                                   m_highlightThemeViewModel, m_rightPanelWidget);
    m_previewPanelWidget->setObjectName(QStringLiteral("previewPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Preview"), m_previewPanelWidget, QStringLiteral("rightPanelPreviewButton"));

    m_monitoringPanelWidget =
        new MonitoringPanelWidget(m_workspaceController->monitoringPanelViewModel(), m_uiThemeChangeNotifier, m_rightPanelWidget);
    m_monitoringPanelWidget->setObjectName(QStringLiteral("monitoringPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Monitoring"), m_monitoringPanelWidget, QStringLiteral("rightPanelMonitoringButton"));
    connect(m_monitoringPanelWidget, &MonitoringPanelWidget::monitoringFailed, this, &MainWindow::onStatusMessage);

    // Media Viewer mode's "Searcher" tab (Architecture.md §14.61): registered up front like the
    // three tabs above, but its header button starts hidden -- it only appears while the mode is
    // active (onMediaViewerModeChanged), toggled via SidePanelWidget::setPanelTabButtonVisible.
    m_mediaViewerSearchPanelWidget = new MediaViewerSearchPanelWidget(m_uiThemeChangeNotifier, m_rightPanelWidget);
    m_mediaViewerSearchPanelWidget->setObjectName(QStringLiteral("mediaViewerSearchPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Searcher"), m_mediaViewerSearchPanelWidget, QStringLiteral("rightPanelSearcherButton"));
    m_rightPanelWidget->setPanelTabButtonVisible(3, false);

    // Window-wide, single instance (like m_searchPanelWidget) -- wired once here. Resolves its
    // target tab via mediaViewerTargetTab() rather than a live focusedTab() lookup: in a split
    // workspace, focus can move to a different tab while this panel is still showing/controlling
    // whichever tab is actually in Media Viewer mode (Architecture.md §14.61).
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::kindChanged, this, [this](MediaViewerKind kind) {
        if (TabViewModel* tab = mediaViewerTargetTab())
        {
            tab->setMediaViewerKind(kind);
        }
    });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::exitRequested, this, [this]() {
        if (TabViewModel* tab = mediaViewerTargetTab())
        {
            tab->exitMediaViewer();
        }
    });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::tagSearchChipActivated, this,
            [this](Tag::Id tagId, Qt::KeyboardModifiers modifiers) {
                if (TabViewModel* tab = mediaViewerTargetTab())
                {
                    tab->handleMediaViewerSearchTagActivated(tagId, modifiers.testFlag(Qt::ControlModifier));
                }
            });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::tagBehaviourChanged, this,
            [this](MediaViewerTagBehaviour behaviour) {
                if (TabViewModel* tab = mediaViewerTargetTab())
                {
                    tab->setMediaViewerTagBehaviour(behaviour);
                }
            });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::tagFilterRemoved, this, [this](Tag::Id tagId) {
        if (TabViewModel* tab = mediaViewerTargetTab())
        {
            tab->removeMediaViewerTagFilter(tagId);
        }
    });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::tagSearchQueryRequested, this, [this](const QString& query) {
        if (TabViewModel* tab = mediaViewerTargetTab())
        {
            m_mediaViewerSearchPanelWidget->setTagSearchResults(tab->searchTagsForCriteria(query));
        }
    });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::tagSearchQueryAccepted, this,
            [this](const QString& query) {
                if (TabViewModel* tab = mediaViewerTargetTab())
                {
                    tab->acceptMediaViewerSearchTagQuery(query);
                }
            });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::tagCreateRequested, this,
            [this](const QString& name) {
                if (TabViewModel* tab = mediaViewerTargetTab())
                {
                    tab->createAndApplyMediaViewerTag(name);
                }
            });
    connect(m_mediaViewerSearchPanelWidget, &MediaViewerSearchPanelWidget::nameFilterChanged, this,
            [this](const QString& query, const NameSearchOptions& options) {
                if (TabViewModel* tab = mediaViewerTargetTab())
                {
                    tab->setMediaViewerNameFilter(query, options);
                }
            });

    // Left panel (Architecture.md §14.41/§14.42): same side-panel mechanics, mirrored, with "Tree"
    // and "Search" tabs.
    m_leftPanelWidget = new SidePanelWidget(SidePanelWidget::Edge::Left, QStringLiteral("leftPanel"), m_uiThemeChangeNotifier, this);
    createFolderTree();
    m_leftPanelWidget->addPanelTab(tr("Tree"), m_folderTreeView, QStringLiteral("leftPanelTreeButton"));
    createSearchPanel();
    m_leftPanelWidget->addPanelTab(tr("Search"), m_searchPanelWidget, QStringLiteral("leftPanelSearchButton"));

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
    constexpr int kLeftPanelTabCount = 2;
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
                                                 m_uiThemeChangeNotifier, m_leftPanelWidget);

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
            tab->exitSearch();
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
            tab->setSearchOptions(options);
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

void MainWindow::showTagManagerDialog()
{
    TagManagerViewModel viewModel(m_tagManagementUseCase, m_tagChangeNotifier, m_advancedSearchService,
                                  m_iconLoadingService, m_fileTagsService);
    TagManagerDialog dialog(&viewModel, m_fileDecorationsViewModel.decorationRules(), m_fileDecorationsViewModel, this);
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
    TextEditorDialog dialog(this);
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
                          m_notificationSettingsViewModel, m_columnVisibilitySettingsViewModel, m_uiThemeChangeNotifier, this);

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

void MainWindow::onMediaViewerModeChanged(bool active)
{
    m_leftPanelWidget->setVisible(!active);

    if (active)
    {
        m_workspaceLayoutWidget->forceSinglePane(m_workspaceController->focusedPane());
        m_layoutActionGroup->setEnabled(false);

        m_rightPanelActiveIndexBeforeMediaViewer = m_rightPanelWidget->activeTabIndex();
        m_rightPanelWidget->setPanelTabButtonVisible(0, false);
        m_rightPanelWidget->setPanelTabButtonVisible(1, false);
        m_rightPanelWidget->setPanelTabButtonVisible(2, false);
        m_rightPanelWidget->setPanelTabButtonVisible(3, true);
        m_rightPanelWidget->activateTab(3);
        return;
    }

    m_workspaceLayoutWidget->clearForcedSinglePane();
    m_layoutActionGroup->setEnabled(true);

    m_rightPanelWidget->setPanelTabButtonVisible(0, true);
    m_rightPanelWidget->setPanelTabButtonVisible(1, true);
    m_rightPanelWidget->setPanelTabButtonVisible(2, true);
    m_rightPanelWidget->setPanelTabButtonVisible(3, false);

    const int restoreIndex = m_rightPanelActiveIndexBeforeMediaViewer;
    m_rightPanelActiveIndexBeforeMediaViewer = -1;
    if (restoreIndex >= 0)
    {
        m_rightPanelWidget->activateTab(restoreIndex);
    }
    else
    {
        // The panel was collapsed before Media Viewer mode took over -- activateTab(3) just now
        // forced it open, so collapse it back the same way a real click on the active tab would
        // (SidePanelWidget::onPanelTabButtonClicked's "clicking the already-active tab collapses
        // the panel" branch).
        m_rightPanelWidget->activateTab(3);
    }
}

TabViewModel* MainWindow::mediaViewerTargetTab() const
{
    return m_activeMediaViewerTab ? m_activeMediaViewerTab : m_workspaceController->focusedTab();
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
        disconnect(m_searchTrackedTab, &TabViewModel::searchFailed, this, &MainWindow::onStatusMessage);
        disconnect(m_searchTrackedTab, &TabViewModel::advancedSearchFailed, this, &MainWindow::onStatusMessage);
        // Every searchResultsChanged/searchModeChanged/searchLoading/searchLoadingFinished/
        // searchFailed connection into m_searchPanelWidget below is a lambda (receiver ==
        // m_searchPanelWidget, never `this`) -- disconnecting all of them by receiver alone is
        // safe and precise, same pattern as MonitoringPanelViewModel::disconnectDisplayedTab.
        disconnect(m_searchTrackedTab, nullptr, m_searchPanelWidget, nullptr);
    }
    m_searchTrackedTab = tab;

    // Media Viewer mode (Architecture.md §14.61): tracks whichever tab is actually in Media
    // Viewer mode (m_activeMediaViewerTab), not the newly-focused tab -- in a split workspace,
    // focus can move to an unrelated tab (a different pane, or another tab in the same pane)
    // while a different tab is still showing Media Viewer mode, forced single-pane. Falls back to
    // the newly-focused tab when no tab is currently in Media Viewer mode, matching the original
    // "track whatever's focused" behavior for the case this was split out of.
    if (TabViewModel* mediaViewerTarget = m_activeMediaViewerTab ? m_activeMediaViewerTab : tab; mediaViewerTarget != m_mediaViewerTrackedTab)
    {
        if (m_mediaViewerTrackedTab)
        {
            disconnect(m_mediaViewerTrackedTab, &TabViewModel::mediaViewerModeChanged, this, nullptr);
            disconnect(m_mediaViewerTrackedTab, &TabViewModel::mediaViewerFailed, this, &MainWindow::onStatusMessage);
            // Every mediaViewerResultsChanged/mediaViewerKindChanged/mediaViewerCriteriaChanged/
            // mediaViewerLoading*/mediaViewerFailed/mediaViewerModeChanged connection into
            // m_mediaViewerSearchPanelWidget or m_mediaViewerModeAction below is a lambda (never a
            // plain `this` member-function connection) -- disconnecting all of them by receiver
            // alone is safe and precise, same pattern as m_searchTrackedTab/m_searchPanelWidget
            // above.
            disconnect(m_mediaViewerTrackedTab, nullptr, m_mediaViewerSearchPanelWidget, nullptr);
            disconnect(m_mediaViewerTrackedTab, nullptr, m_mediaViewerModeAction, nullptr);
        }
        m_mediaViewerTrackedTab = mediaViewerTarget;

        if (TabViewModel* viewerTab = m_mediaViewerTrackedTab)
        {
            {
                const QSignalBlocker mediaViewerActionBlocker(m_mediaViewerModeAction);
                m_mediaViewerModeAction->setChecked(viewerTab->mediaViewerActive());
            }

            connect(viewerTab, &TabViewModel::mediaViewerModeChanged, this, [this, viewerTab](bool active) {
                if (active)
                {
                    m_activeMediaViewerTab = viewerTab;
                }
                else if (m_activeMediaViewerTab == viewerTab)
                {
                    m_activeMediaViewerTab = nullptr;
                }
                onMediaViewerModeChanged(active);
            });
            connect(viewerTab, &TabViewModel::mediaViewerFailed, this, &MainWindow::onStatusMessage);

            connect(viewerTab, &TabViewModel::mediaViewerModeChanged, m_mediaViewerModeAction, [this](bool active) {
                const QSignalBlocker blocker(m_mediaViewerModeAction);
                m_mediaViewerModeAction->setChecked(active);
            });

            connect(viewerTab, &TabViewModel::mediaViewerKindChanged, m_mediaViewerSearchPanelWidget,
                    [this](MediaViewerKind kind) { m_mediaViewerSearchPanelWidget->setKind(kind); });
            connect(viewerTab, &TabViewModel::mediaViewerCriteriaChanged, m_mediaViewerSearchPanelWidget, [this, viewerTab]() {
                m_mediaViewerSearchPanelWidget->setAppliedTagFilters(viewerTab->resolveMediaViewerTagFilters());
            });
            connect(viewerTab, &TabViewModel::mediaViewerTagBehaviourChanged, m_mediaViewerSearchPanelWidget,
                    [this](MediaViewerTagBehaviour behaviour) { m_mediaViewerSearchPanelWidget->setTagBehaviour(behaviour); });
            connect(viewerTab, &TabViewModel::mediaViewerLoading, m_mediaViewerSearchPanelWidget,
                    [this]() { m_mediaViewerSearchPanelWidget->setLoading(true); });
            connect(viewerTab, &TabViewModel::mediaViewerLoadingFinished, m_mediaViewerSearchPanelWidget,
                    [this]() { m_mediaViewerSearchPanelWidget->setLoading(false); });
            connect(viewerTab, &TabViewModel::mediaViewerFailed, m_mediaViewerSearchPanelWidget,
                    [this]() { m_mediaViewerSearchPanelWidget->setLoading(false); });

            m_mediaViewerSearchPanelWidget->setKind(viewerTab->mediaViewerKind());
            m_mediaViewerSearchPanelWidget->setTagBehaviour(viewerTab->mediaViewerTagBehaviour());
            m_mediaViewerSearchPanelWidget->setAppliedTagFilters(viewerTab->resolveMediaViewerTagFilters());
            m_mediaViewerSearchPanelWidget->setNameFilter(viewerTab->mediaViewerNameFilter(), viewerTab->mediaViewerNameOptions());
            // Prepopulate with every tag (searchTagsForCriteria("") -> all tags, alphabetically)
            // rather than leaving the matching-tags section empty until the user types something.
            m_mediaViewerSearchPanelWidget->setTagSearchResults(viewerTab->searchTagsForCriteria(QString()));
        }
        else
        {
            const QSignalBlocker mediaViewerActionBlocker(m_mediaViewerModeAction);
            m_mediaViewerModeAction->setChecked(false);
        }
    }

    if (m_driveInfoTrackedTab)
    {
        disconnect(m_driveInfoTrackedTab, &TabViewModel::currentPathChanged, this, &MainWindow::onActiveTabPathChanged);
    }
    m_driveInfoTrackedTab = tab;

    if (m_favoritesTrackedTab)
    {
        disconnect(m_favoritesTrackedTab, &TabViewModel::selectedEntriesChanged, this, &MainWindow::onFavoritesTrackedSelectionChanged);
        disconnect(m_favoritesTrackedTab, &TabViewModel::currentPathChanged, this, &MainWindow::onFavoritesTrackedPathChanged);
    }
    m_favoritesTrackedTab = tab;

    if (!tab)
    {
        m_sortByMenu->setEnabled(false);
        m_searchPanelWidget->clearForNoTab();
        // Still enabled/checked against m_activeMediaViewerTab's own state when one exists -- it
        // may still be showing Media Viewer mode on another pane even though this pane has no
        // focused tab (handled by the mediaViewerTarget block above; nothing else to do here).
        if (!m_activeMediaViewerTab)
        {
            const QSignalBlocker mediaViewerActionBlocker(m_mediaViewerModeAction);
            m_mediaViewerModeAction->setEnabled(false);
        }
        onActiveTabPathChanged({});
        updateFavoritesActionEnabled();
        return;
    }

    m_sortByMenu->setEnabled(true);
    connect(tab, &TabViewModel::sortOrderChanged, this, &MainWindow::onSortOrderChanged);
    onSortOrderChanged(tab->sortCriterion(), tab->sortAscending());

    connect(tab, &TabViewModel::searchFailed, this, &MainWindow::onStatusMessage);
    connect(tab, &TabViewModel::advancedSearchFailed, this, &MainWindow::onStatusMessage);

    connect(tab, &TabViewModel::searchResultsChanged, m_searchPanelWidget, [this, tab](const std::vector<FileNode>& entries) {
        m_searchPanelWidget->setResults(entries, tab->searchResultsModel()->currentDirectory(), tab->searchResultIcons());
    });
    // searchResultsChanged alone misses icons that resolve after the candidate list itself last
    // changed (the common case -- IconLoadingService's batches arrive asynchronously, well after
    // applySearchFilter already emitted) -- see TabViewModel::searchResultIconsChanged.
    connect(tab, &TabViewModel::searchResultIconsChanged, m_searchPanelWidget, [this, tab]() {
        m_searchPanelWidget->applyIcons(tab->searchResultIcons());
    });
    connect(tab, &TabViewModel::searchModeChanged, m_searchPanelWidget, [this](bool active) {
        if (!active)
        {
            m_searchPanelWidget->setResults({}, {});
        }
    });
    connect(tab, &TabViewModel::searchLoading, m_searchPanelWidget, [this]() { m_searchPanelWidget->setLoading(true); });
    connect(tab, &TabViewModel::searchLoadingFinished, m_searchPanelWidget, [this]() { m_searchPanelWidget->setLoading(false); });
    connect(tab, &TabViewModel::searchFailed, m_searchPanelWidget, [this]() { m_searchPanelWidget->setLoading(false); });

    m_searchPanelWidget->setQuery(tab->searchActive() ? tab->searchQuery() : QString());
    m_searchPanelWidget->setOptions(tab->searchOptions());
    if (tab->searchActive())
    {
        m_searchPanelWidget->setResults(tab->searchResultsModel()->entries(), tab->searchResultsModel()->currentDirectory(),
                                         tab->searchResultIcons());
    }
    else
    {
        m_searchPanelWidget->setResults({}, {});
    }

    // Media Viewer mode (Architecture.md §14.61): the toggle action is always enabled once any tab
    // is focused -- entering Media Viewer mode targets the focused tab (createMediaViewerModeAction()),
    // independent of which tab mediaViewerTargetTab() currently resolves to above.
    m_mediaViewerModeAction->setEnabled(true);

    connect(tab, &TabViewModel::currentPathChanged, this, &MainWindow::onActiveTabPathChanged);
    onActiveTabPathChanged(tab->currentPath());

    connect(tab, &TabViewModel::selectedEntriesChanged, this, &MainWindow::onFavoritesTrackedSelectionChanged);
    connect(tab, &TabViewModel::currentPathChanged, this, &MainWindow::onFavoritesTrackedPathChanged);
    updateFavoritesActionEnabled();
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

void MainWindow::onAddToFavoritesTriggered()
{
    TabViewModel* tab = m_workspaceController->focusedTab();
    if (!tab)
    {
        return;
    }

    const std::vector<FileNode>& selected = tab->selectedEntries();
    if (selected.empty())
    {
        // Falls back to the focused tab's current browsed folder itself, mirroring the Tag
        // Panel's "no selection" convention (Architecture.md §14.9/§14.35).
        if (tab->currentPath() != VirtualPaths::ThisPC)
        {
            m_favoritesViewModel.addItem(tab->currentPath());
        }
        return;
    }

    // Every selected entry becomes its own top-level, uncategorized favorite (Architecture.md
    // §14.35) -- categorization only happens from the Manage Favorites dialog.
    for (const FileNode& entry : selected)
    {
        m_favoritesViewModel.addItem(entry.path());
    }
}

void MainWindow::onFavoriteItemTriggered(Favorites::Id id)
{
    std::optional<std::filesystem::path> targetPath;
    for (const Favorites::Entry& entry : m_favoritesViewModel.entries())
    {
        if (const auto* item = std::get_if<FavoriteItem>(&entry); item && item->id() == id)
        {
            targetPath = item->path();
            break;
        }
        if (const auto* category = std::get_if<FavoriteCategory>(&entry))
        {
            const auto& items = category->items();
            const auto it = std::find_if(items.begin(), items.end(), [id](const FavoriteItem& item) { return item.id() == id; });
            if (it != items.end())
            {
                targetPath = it->path();
                break;
            }
        }
    }

    if (!targetPath)
    {
        return;
    }

    WorkspacePaneWidget* paneWidget = m_workspaceLayoutWidget->paneWidget(m_workspaceController->focusedPane());
    if (!paneWidget)
    {
        return;
    }

    const std::filesystem::path path = *targetPath;
    QPointer<WorkspacePaneWidget> paneGuard(paneWidget);

    // FileNavigationUseCase::stat() is disk I/O and must never run on the Qt UI thread -- reuses
    // m_driveInfoWorker/m_driveInfoThread (Architecture.md §14.30) rather than spinning up a
    // dedicated thread just for this one call site. Both hops are queued functor calls, the same
    // shape onActiveTabPathChanged/onDriveSpaceInfoReady already use.
    QMetaObject::invokeMethod(
        m_driveInfoWorker,
        [this, paneGuard, path]() {
            Result<FileNode> stat = m_driveInfoWorker->stat(path);

            QMetaObject::invokeMethod(
                this,
                [this, paneGuard, path, stat = std::move(stat)]() mutable {
                    if (!paneGuard)
                    {
                        // The focused pane changed/closed while the stat was in flight.
                        return;
                    }

                    if (!stat)
                    {
                        // Target no longer exists on disk -- surfaces through the same status-bar
                        // path as any other bad navigation target (Architecture.md §14.35), no modal.
                        onStatusMessage(QString::fromStdString(stat.error().message));
                        return;
                    }

                    if (stat.value().isDirectory())
                    {
                        // Folder favorite: navigate into it (Explorer Quick-Access-style).
                        paneGuard->navigateAndSelect(path, std::nullopt);
                    }
                    else
                    {
                        // File favorite: navigate to its parent folder with the file selected.
                        paneGuard->navigateAndSelect(path.parent_path(), path);
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void MainWindow::rebuildFavoritesMenuItems()
{
    // The first 3 actions (Add to Favorites, Manage Favorites..., separator) are static, added
    // once by createFavoritesMenu() -- only entries after them are torn down and rebuilt here.
    constexpr int kStaticActionCount = 3;

    const QList<QAction*> actions = m_favoritesMenu->actions();
    for (int i = actions.size() - 1; i >= kStaticActionCount; --i)
    {
        QAction* action = actions[i];
        m_favoritesMenu->removeAction(action);
        if (QMenu* submenu = action->menu())
        {
            // Deleting the submenu also deletes its owned menuAction() (the action itself).
            submenu->deleteLater();
        }
        else
        {
            action->deleteLater();
        }
    }

    // Adds ONE menu entry per favorite item -- a QWidgetAction whose FavoriteMenuRowWidget renders
    // the name and, below it, the smaller/grayed full path -- rather than a separate action per
    // line. menu's hovered() signal drives the row's highlight repaint since QMenu itself paints
    // nothing behind a widget-action row (see FavoriteMenuRowWidget's comment above).
    auto addFavoriteItemAction = [this](QMenu* menu, const FavoriteItem& favoriteItem)
    {
        const Favorites::Id id = favoriteItem.id();
        const QString name = QString::fromStdWString(favoriteItem.displayName().wstring());
        const QString path = QString::fromStdWString(favoriteItem.path().wstring());

        auto* rowWidget = new FavoriteMenuRowWidget(favoriteItemIcon(favoriteItem.path()), name, path);

        auto* action = new QWidgetAction(menu);
        action->setToolTip(path);
        action->setDefaultWidget(rowWidget);
        connect(action, &QAction::triggered, this, [this, id]() { onFavoriteItemTriggered(id); });
        menu->addAction(action);

        connect(menu, &QMenu::hovered, rowWidget, [rowWidget, action](QAction* hoveredAction) {
            rowWidget->setHighlighted(hoveredAction == action);
        });
    };

    for (const Favorites::Entry& entry : m_favoritesViewModel.entries())
    {
        if (const auto* item = std::get_if<FavoriteItem>(&entry))
        {
            addFavoriteItemAction(m_favoritesMenu, *item);
        }
        else if (const auto* category = std::get_if<FavoriteCategory>(&entry))
        {
            QMenu* submenu = m_favoritesMenu->addMenu(favoriteCategoryIcon(), QString::fromStdString(category->name()));
            for (const FavoriteItem& categoryItem : category->items())
            {
                addFavoriteItemAction(submenu, categoryItem);
            }
        }
    }
}

void MainWindow::updateFavoritesActionEnabled()
{
    const bool enabled = m_favoritesTrackedTab && m_favoritesTrackedTab->currentPath() != VirtualPaths::ThisPC;
    m_addToFavoritesAction->setEnabled(enabled);
}

void MainWindow::onFavoritesTrackedSelectionChanged(const std::vector<FileNode>&)
{
    updateFavoritesActionEnabled();
}

void MainWindow::onFavoritesTrackedPathChanged(const std::filesystem::path&)
{
    updateFavoritesActionEnabled();
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
            if (pane->tabAt(i)->isMonitoringActive())
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
