#include "MainWindow.h"

#include <algorithm>
#include <array>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
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
#include <QResizeEvent>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "AppConfigStore.h"
#include "DriveSpaceInfoWorker.h"
#include "FavoritesViewModel.h"
#include "FileOperationsController.h"
#include "ManageFavoritesDialog.h"
#include "MainStatusBarWidget.h"
#include "MonitoringPanelWidget.h"
#include "NotificationOverlayWidget.h"
#include "SettingsDialog.h"
#include "TabViewModel.h"
#include "TagManagerDialog.h"
#include "TagManagerViewModel.h"
#include "PreviewPanelWidget.h"
#include "RightPanelWidget.h"
#include "TagPanelWidget.h"
#include "UiColors.h"
#include "VirtualPaths.h"
#include "WorkspaceController.h"
#include "WorkspaceLayoutWidget.h"
#include "WorkspacePaneId.h"
#include "WorkspacePaneViewModel.h"
#include "WorkspacePaneWidget.h"

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
        }
        return QString();
    }
}

MainWindow::MainWindow(WorkspaceController* workspaceController, AppConfigStore& configStore,
                         TagManagementUseCase& tagManagementUseCase, TagChangeNotifier& tagChangeNotifier,
                         SyntaxHighlightEngine& syntaxHighlightEngine,
                         HighlightThemeViewModel& highlightThemeViewModel, FileDecorationsViewModel& fileDecorationsViewModel,
                         FavoritesViewModel& favoritesViewModel, NotificationCenter& notificationCenter,
                         NotificationSettingsViewModel& notificationSettingsViewModel,
                         ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel, const AppConfig& initialConfig,
                         QWidget* parent)
    : QMainWindow(parent)
    , m_workspaceController(workspaceController)
    , m_configStore(configStore)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_syntaxHighlightEngine(syntaxHighlightEngine)
    , m_highlightThemeViewModel(highlightThemeViewModel)
    , m_fileDecorationsViewModel(fileDecorationsViewModel)
    , m_favoritesViewModel(favoritesViewModel)
    , m_notificationCenter(notificationCenter)
    , m_notificationSettingsViewModel(notificationSettingsViewModel)
    , m_columnVisibilitySettingsViewModel(columnVisibilitySettingsViewModel)
    , m_columnWidths(initialConfig.detailsColumnWidths)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Exp-LORer"));

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
    });
}

void MainWindow::createMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Settings..."), this, &MainWindow::showSettingsDialog);
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

    createFavoritesMenu();
    menuBar()->addMenu(tr("&Tools"));
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

    createSearchBar(toolBar);
}

void MainWindow::createSearchBar(QToolBar* toolBar)
{
    m_searchBar = new QLineEdit(toolBar);
    m_searchBar->setObjectName(QStringLiteral("globalSearchBar"));
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setPlaceholderText(tr("Search this folder..."));
    m_searchBar->installEventFilter(this);
    toolBar->addWidget(m_searchBar);

    connect(m_searchBar, &QLineEdit::returnPressed, this, &MainWindow::onSearchBarReturnPressed);
    connect(m_searchBar, &QLineEdit::textEdited, this, &MainWindow::onSearchBarTextEdited);

    m_advancedSearchAction = toolBar->addAction(tr("Advanced Search..."));
    connect(m_advancedSearchAction, &QAction::triggered, this, [this]() {
        if (TabViewModel* tab = m_workspaceController->focusedTab())
        {
            tab->showAdvancedSearchPanel();
        }
    });

    if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(m_advancedSearchAction)))
    {
        button->setObjectName(QStringLiteral("advancedSearchButton"));
        button->setStyleSheet(QStringLiteral(
            "QToolButton#advancedSearchButton { background: %1; border: 1px solid %2; border-radius: 3px; }"
            "QToolButton#advancedSearchButton:hover { background: %3; }")
            .arg(QLatin1String(UiColors::kAdvancedSearchButtonBackground), QLatin1String(UiColors::kAccentBlue),
                 QLatin1String(UiColors::kAdvancedSearchButtonHoverBackground)));
    }
}

void MainWindow::createWorkspace()
{
    m_workspaceLayoutWidget =
        new WorkspaceLayoutWidget(m_workspaceController, m_columnVisibilitySettingsViewModel, m_columnWidths, this);
    m_workspaceLayoutWidget->setObjectName(QStringLiteral("workspaceLayoutWidget"));

    // Right panel (Architecture.md §14.23): the side-panel counterpart of BottomPanelWidget, a
    // collapsible tab strip with a single "Tags" tab, wrapping the existing tag UI instead of
    // placing it directly in the splitter.
    m_rightPanelWidget = new RightPanelWidget(this);
    m_rightPanelWidget->setObjectName(QStringLiteral("rightPanelWidget"));
    m_tagPanelWidget = new TagPanelWidget(m_workspaceController->tagListViewModel(), m_rightPanelWidget);
    m_tagPanelWidget->setObjectName(QStringLiteral("tagPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Tags"), m_tagPanelWidget);

    m_previewPanelWidget = new PreviewPanelWidget(m_workspaceController->filePreviewViewModel(), m_syntaxHighlightEngine,
                                                   m_highlightThemeViewModel, m_rightPanelWidget);
    m_previewPanelWidget->setObjectName(QStringLiteral("previewPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Preview"), m_previewPanelWidget);

    m_monitoringPanelWidget = new MonitoringPanelWidget(m_workspaceController->monitoringPanelViewModel(), m_rightPanelWidget);
    m_monitoringPanelWidget->setObjectName(QStringLiteral("monitoringPanelWidget"));
    m_rightPanelWidget->addPanelTab(tr("Monitoring"), m_monitoringPanelWidget);
    connect(m_monitoringPanelWidget, &MonitoringPanelWidget::monitoringFailed, this, &MainWindow::onStatusMessage);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    splitter->addWidget(m_workspaceLayoutWidget);
    splitter->addWidget(m_rightPanelWidget);
    splitter->setCollapsible(0, false);
    splitter->setSizes({ 1000, m_rightPanelWidget->collapsedWidth() });
    setCentralWidget(splitter);

    connect(m_rightPanelWidget, &RightPanelWidget::expansionChanged, splitter, [splitter, rightPanel = m_rightPanelWidget](bool expanded) {
        const int total = std::max(splitter->width(), splitter->sizeHint().width());
        const int rightWidth = expanded ? rightPanel->preferredExpandedWidth() : rightPanel->collapsedWidth();
        splitter->setSizes({ total - rightWidth, rightWidth });
    });

    for (WorkspacePaneId id : kAllPanes)
    {
        connect(m_workspaceLayoutWidget->paneWidget(id), &WorkspacePaneWidget::navigationFailed, this, &MainWindow::onStatusMessage);
        connect(m_workspaceLayoutWidget->paneWidget(id), &WorkspacePaneWidget::gitOperationFailed, this, &MainWindow::onStatusMessage);
    }

    connect(m_workspaceController->fileOperationsController(), &FileOperationsController::operationFailed, this,
            &MainWindow::onStatusMessage);
}

void MainWindow::createMainStatusBar()
{
    m_mainStatusBarWidget = new MainStatusBarWidget(this);
    m_mainStatusBarWidget->setObjectName(QStringLiteral("mainStatusBarWidget"));
    statusBar()->addPermanentWidget(m_mainStatusBarWidget, 1);
}

void MainWindow::showTagManagerDialog()
{
    TagManagerViewModel viewModel(m_tagManagementUseCase, m_tagChangeNotifier);
    TagManagerDialog dialog(&viewModel, this);
    dialog.exec();
}

void MainWindow::showManageFavoritesDialog()
{
    ManageFavoritesDialog dialog(&m_favoritesViewModel, this);
    dialog.exec();
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(m_syntaxHighlightEngine, m_highlightThemeViewModel, m_fileDecorationsViewModel,
                          m_notificationSettingsViewModel, m_columnVisibilitySettingsViewModel, this);

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
        disconnect(m_searchTrackedTab, &TabViewModel::searchFailed, this, &MainWindow::onStatusMessage);
        disconnect(m_searchTrackedTab, &TabViewModel::advancedSearchFailed, this, &MainWindow::onStatusMessage);
    }
    m_searchTrackedTab = tab;

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
        m_searchBar->clear();
        onActiveTabPathChanged({});
        updateFavoritesActionEnabled();
        return;
    }

    m_sortByMenu->setEnabled(true);
    connect(tab, &TabViewModel::sortOrderChanged, this, &MainWindow::onSortOrderChanged);
    onSortOrderChanged(tab->sortCriterion(), tab->sortAscending());

    connect(tab, &TabViewModel::searchFailed, this, &MainWindow::onStatusMessage);
    connect(tab, &TabViewModel::advancedSearchFailed, this, &MainWindow::onStatusMessage);
    m_searchBar->setText(tab->searchActive() ? tab->searchQuery() : QString());

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

void MainWindow::onSearchBarReturnPressed()
{
    TabViewModel* tab = m_workspaceController->focusedTab();
    if (!tab)
    {
        return;
    }
    tab->startSearch(m_searchBar->text());
}

void MainWindow::onSearchBarTextEdited(const QString& text)
{
    TabViewModel* tab = m_workspaceController->focusedTab();
    if (!tab)
    {
        return;
    }

    if (text.trimmed().isEmpty())
    {
        tab->exitSearch();
        return;
    }

    if (tab->searchActive())
    {
        tab->updateSearchQuery(text);
    }
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_searchBar && event->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape)
        {
            m_searchBar->clear();
            if (TabViewModel* tab = m_workspaceController->focusedTab())
            {
                tab->exitSearch();
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
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

    AppConfig config;
    config.windowGeometry = saveGeometry();
    config.workspace = m_workspaceController->captureConfig();

    if (WorkspacePaneWidget* focusedPaneWidget = m_workspaceLayoutWidget->paneWidget(m_workspaceController->focusedPane()))
    {
        m_columnWidths = focusedPaneWidget->currentColumnWidths();
    }
    config.detailsColumnWidths = m_columnWidths;

    m_configStore.save(config);

    QMainWindow::closeEvent(event);
}
