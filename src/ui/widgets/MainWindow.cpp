#include "MainWindow.h"

#include <algorithm>
#include <array>

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>

#include "FileOperationsController.h"
#include "TabViewModel.h"
#include "TagPanelWidget.h"
#include "WorkspaceController.h"
#include "WorkspaceLayoutWidget.h"
#include "WorkspacePaneId.h"
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

MainWindow::MainWindow(WorkspaceController* workspaceController, QWidget* parent)
    : QMainWindow(parent)
    , m_workspaceController(workspaceController)
{
    setWindowTitle(tr("Exp-LORer"));
    resize(1024, 768);

    createLayoutActions();
    createSortByActions();
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

void MainWindow::createMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    menuBar()->addMenu(tr("&Edit"));

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    QMenu* layoutMenu = viewMenu->addMenu(tr("&Layout"));
    layoutMenu->addActions(m_layoutActions);

    m_sortByMenu = viewMenu->addMenu(tr("&Sort by"));
    m_sortByMenu->addActions(m_sortCriterionActions);
    m_sortByMenu->addSeparator();
    m_sortByMenu->addAction(m_ascendingAction);
    m_sortByMenu->addAction(m_descendingAction);

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
}

void MainWindow::createWorkspace()
{
    m_workspaceLayoutWidget = new WorkspaceLayoutWidget(m_workspaceController, this);
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

    if (!tab)
    {
        m_sortByMenu->setEnabled(false);
        return;
    }

    m_sortByMenu->setEnabled(true);
    connect(tab, &TabViewModel::sortOrderChanged, this, &MainWindow::onSortOrderChanged);
    onSortOrderChanged(tab->sortCriterion(), tab->sortAscending());
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
