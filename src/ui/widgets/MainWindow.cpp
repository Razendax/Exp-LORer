#include "MainWindow.h"

#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>

#include "NavigationViewModel.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }
}

MainWindow::MainWindow(NavigationViewModel* navigationViewModel, QWidget* parent)
    : QMainWindow(parent)
    , m_navigationViewModel(navigationViewModel)
{
    setWindowTitle(tr("Exp-LORer"));
    resize(1024, 768);

    createMenuBar();
    createNavigationToolBar();
    createTabArea();

    connect(m_navigationViewModel, &NavigationViewModel::currentPathChanged, this, &MainWindow::onCurrentPathChanged);
    connect(m_navigationViewModel, &NavigationViewModel::backAvailableChanged, m_backAction, &QAction::setEnabled);
    connect(m_navigationViewModel, &NavigationViewModel::forwardAvailableChanged, m_forwardAction, &QAction::setEnabled);
    connect(m_navigationViewModel, &NavigationViewModel::upAvailableChanged, m_upAction, &QAction::setEnabled);
    connect(m_navigationViewModel, &NavigationViewModel::navigationFailed, this, &MainWindow::onNavigationFailed);
}

void MainWindow::createMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    menuBar()->addMenu(tr("&Edit"));
    menuBar()->addMenu(tr("&View"));
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

    m_tabWidget->addTab(new QWidget(m_tabWidget), tr("This PC"));

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
