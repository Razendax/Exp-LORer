#include "MainWindow.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Exp-LORer"));
    resize(1024, 768);

    createMenuBar();
    createNavigationToolBar();
    createTabArea();
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

    toolBar->addAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("Back"));
    toolBar->addAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("Forward"));
    toolBar->addAction(style()->standardIcon(QStyle::SP_FileDialogToParent), tr("Up"));
    toolBar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh"));
}

void MainWindow::createTabArea()
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(false);
    m_tabWidget->setMovable(false);

    m_tabWidget->addTab(new QWidget(m_tabWidget), tr("This PC"));

    setCentralWidget(m_tabWidget);
}
