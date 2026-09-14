#include "MainWindow.h"

#include <algorithm>
#include <array>

#include <QAction>
#include <QActionGroup>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "FileBrowserView.h"
#include "FileListModel.h"
#include "NavigationViewModel.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // Order shared with MainWindow::m_viewModeActions: element i of one is the action for
    // element i of the other.
    constexpr std::array<ViewMode, 7> kViewModes = {
        ViewMode::ExtraLargeIcons,
        ViewMode::LargeIcons,
        ViewMode::MediumIcons,
        ViewMode::SmallIcons,
        ViewMode::List,
        ViewMode::Details,
        ViewMode::Tiles,
    };

    QString viewModeLabel(ViewMode mode)
    {
        switch (mode)
        {
            case ViewMode::ExtraLargeIcons:
                return QObject::tr("Extra large icons");
            case ViewMode::LargeIcons:
                return QObject::tr("Large icons");
            case ViewMode::MediumIcons:
                return QObject::tr("Medium icons");
            case ViewMode::SmallIcons:
                return QObject::tr("Small icons");
            case ViewMode::List:
                return QObject::tr("List");
            case ViewMode::Details:
                return QObject::tr("Details");
            case ViewMode::Tiles:
                return QObject::tr("Tiles");
        }
        return QString();
    }
}

MainWindow::MainWindow(NavigationViewModel* navigationViewModel, QWidget* parent)
    : QMainWindow(parent)
    , m_navigationViewModel(navigationViewModel)
{
    setWindowTitle(tr("Exp-LORer"));
    resize(1024, 768);

    createViewModeActions();
    createMenuBar();
    createNavigationToolBar();
    createTabArea();

    connect(m_navigationViewModel, &NavigationViewModel::currentPathChanged, this, &MainWindow::onCurrentPathChanged);
    connect(m_navigationViewModel, &NavigationViewModel::directoryContentsChanged, m_fileListModel, &FileListModel::setEntries);
    connect(m_navigationViewModel, &NavigationViewModel::backAvailableChanged, m_backAction, &QAction::setEnabled);
    connect(m_navigationViewModel, &NavigationViewModel::forwardAvailableChanged, m_forwardAction, &QAction::setEnabled);
    connect(m_navigationViewModel, &NavigationViewModel::upAvailableChanged, m_upAction, &QAction::setEnabled);
    connect(m_navigationViewModel, &NavigationViewModel::navigationFailed, this, &MainWindow::onNavigationFailed);
    connect(m_navigationViewModel, &NavigationViewModel::viewModeChanged, this, &MainWindow::onViewModeChanged);
    connect(m_fileBrowserView, &FileBrowserView::itemActivated, this, &MainWindow::onItemActivated);

    onViewModeChanged(m_navigationViewModel->viewMode());
}

void MainWindow::createViewModeActions()
{
    m_viewModeActionGroup = new QActionGroup(this);
    m_viewModeActionGroup->setExclusive(true);

    for (ViewMode mode : kViewModes)
    {
        QAction* action = new QAction(viewModeLabel(mode), this);
        action->setCheckable(true);
        m_viewModeActionGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode]() { m_navigationViewModel->setViewMode(mode); });
        m_viewModeActions.append(action);
    }
}

void MainWindow::createMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    menuBar()->addMenu(tr("&Edit"));

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addActions(m_viewModeActions);

    menuBar()->addMenu(tr("F&avorites"));
    menuBar()->addMenu(tr("&Tools"));
    menuBar()->addMenu(tr("&Help"));
}

void MainWindow::createNavigationToolBar()
{
    QToolBar* toolBar = addToolBar(tr("Navigation"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_backAction = toolBar->addAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("Back"));
    m_forwardAction = toolBar->addAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("Forward"));
    m_upAction = toolBar->addAction(style()->standardIcon(QStyle::SP_FileDialogToParent), tr("Up"));

    m_backAction->setEnabled(false);
    m_forwardAction->setEnabled(false);
    m_upAction->setEnabled(false);

    auto* viewModeMenu = new QMenu(this);
    viewModeMenu->addActions(m_viewModeActions);

    auto* viewModeButton = new QToolButton(toolBar);
    viewModeButton->setPopupMode(QToolButton::InstantPopup);
    viewModeButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    viewModeButton->setToolTip(tr("View"));
    viewModeButton->setMenu(viewModeMenu);
    toolBar->addWidget(viewModeButton);

    m_addressBar = new QLineEdit(toolBar);
    m_addressBar->setClearButtonEnabled(true);
    toolBar->addWidget(m_addressBar);

    toolBar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh"));

    connect(m_backAction, &QAction::triggered, m_navigationViewModel, &NavigationViewModel::goBack);
    connect(m_forwardAction, &QAction::triggered, m_navigationViewModel, &NavigationViewModel::goForward);
    connect(m_upAction, &QAction::triggered, m_navigationViewModel, &NavigationViewModel::goUp);
    connect(m_addressBar, &QLineEdit::returnPressed, this, &MainWindow::onAddressBarEdited);
}

void MainWindow::createTabArea()
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(false);
    m_tabWidget->setMovable(false);

    m_fileListModel = new FileListModel(this);
    m_fileBrowserView = new FileBrowserView(m_fileListModel, m_tabWidget);

    m_tabWidget->addTab(m_fileBrowserView, tr("This PC"));

    setCentralWidget(m_tabWidget);
}

void MainWindow::onCurrentPathChanged(const std::filesystem::path& path)
{
    m_addressBar->setText(toQString(path));
}

void MainWindow::onAddressBarEdited()
{
    m_navigationViewModel->navigateTo(std::filesystem::path(m_addressBar->text().toStdWString()));
}

void MainWindow::onNavigationFailed(const std::filesystem::path& path, const QString& message)
{
    Q_UNUSED(path);

    statusBar()->showMessage(message, 5000);
    m_addressBar->setText(toQString(m_navigationViewModel->currentPath()));
}

void MainWindow::onViewModeChanged(ViewMode mode)
{
    m_fileBrowserView->setViewMode(mode);

    const auto it = std::find(kViewModes.begin(), kViewModes.end(), mode);
    if (it == kViewModes.end())
    {
        return;
    }

    const auto index = std::distance(kViewModes.begin(), it);
    m_viewModeActions[static_cast<int>(index)]->setChecked(true);
}

void MainWindow::onItemActivated(const std::filesystem::path& path, bool isDirectory)
{
    if (!isDirectory)
    {
        return;
    }

    m_navigationViewModel->navigateTo(path);
}
