#include "MainWindow.h"

#include <algorithm>
#include <array>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>

#include "AppConfigStore.h"
#include "FileOperationsController.h"
#include "TabViewModel.h"
#include "TagManagerDialog.h"
#include "TagManagerViewModel.h"
#include "TagPanelWidget.h"
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
                         TagManagementUseCase& tagManagementUseCase, const AppConfig& initialConfig, QWidget* parent)
    : QMainWindow(parent)
    , m_workspaceController(workspaceController)
    , m_configStore(configStore)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_columnWidths(initialConfig.detailsColumnWidths)
{
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

    connect(m_workspaceController, &WorkspaceController::layoutChanged, this, &MainWindow::onLayoutChanged);
    connect(m_workspaceController, &WorkspaceController::focusedTabChanged, this, &MainWindow::onFocusedTabChanged);

    onLayoutChanged(m_workspaceController->layout());
    onFocusedTabChanged(m_workspaceController->focusedTab());
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

    menuBar()->addMenu(tr("F&avorites"));
    menuBar()->addMenu(tr("&Tools"));
    menuBar()->addMenu(tr("&Help"));
}

void MainWindow::createLayoutToolBar()
{
    QToolBar* toolBar = addToolBar(tr("Layout"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar->addActions(m_layoutActions);

    createSearchBar(toolBar);
}

void MainWindow::createSearchBar(QToolBar* toolBar)
{
    m_searchBar = new QLineEdit(toolBar);
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
}

void MainWindow::createWorkspace()
{
    m_workspaceLayoutWidget = new WorkspaceLayoutWidget(m_workspaceController, m_columnWidths, this);
    m_tagPanelWidget = new TagPanelWidget(m_workspaceController->tagListViewModel(), this);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_workspaceLayoutWidget);
    splitter->addWidget(m_tagPanelWidget);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    for (WorkspacePaneId id : kAllPanes)
    {
        connect(m_workspaceLayoutWidget->paneWidget(id), &WorkspacePaneWidget::navigationFailed, this, &MainWindow::onStatusMessage);
    }

    connect(m_workspaceController->fileOperationsController(), &FileOperationsController::operationFailed, this,
            &MainWindow::onStatusMessage);
}

void MainWindow::showTagManagerDialog()
{
    TagManagerViewModel viewModel(m_tagManagementUseCase);
    TagManagerDialog dialog(&viewModel, this);
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
    }
    m_searchTrackedTab = tab;

    if (!tab)
    {
        m_sortByMenu->setEnabled(false);
        m_searchBar->clear();
        return;
    }

    m_sortByMenu->setEnabled(true);
    connect(tab, &TabViewModel::sortOrderChanged, this, &MainWindow::onSortOrderChanged);
    onSortOrderChanged(tab->sortCriterion(), tab->sortAscending());

    connect(tab, &TabViewModel::searchFailed, this, &MainWindow::onStatusMessage);
    m_searchBar->setText(tab->searchActive() ? tab->searchQuery() : QString());
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
