#include "WorkspacePaneWidget.h"

#include <algorithm>
#include <array>

#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "ContextMenuBuilder.h"
#include "FileBrowserView.h"
#include "FileOperationsController.h"
#include "IContextMenuProvider.h"
#include "TabViewModel.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    QString tabLabelFor(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return QObject::tr("New tab");
        }

        const QString name = toQString(path.filename());
        return name.isEmpty() ? toQString(path) : name;
    }

    // Order shared with WorkspacePaneWidget::m_viewModeActions: element i of one is the action
    // for element i of the other. Each pane owns its own QActionGroup/QAction instances, so this
    // mirrors (rather than shares) MainWindow.cpp's former single toolbar array.
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

WorkspacePaneWidget::WorkspacePaneWidget(WorkspacePaneViewModel* pane, FileOperationsController* fileOperationsController,
                                          QWidget* parent)
    : QWidget(parent)
    , m_pane(pane)
    , m_fileOperationsController(fileOperationsController)
{
    createViewModeActions();
    QToolBar* toolBar = createToolBar();
    createTabArea();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolBar);
    layout->addWidget(m_tabWidget);

    connect(m_pane, &WorkspacePaneViewModel::tabAdded, this, &WorkspacePaneWidget::onTabAdded);
    connect(m_pane, &WorkspacePaneViewModel::tabClosed, this, &WorkspacePaneWidget::onTabClosed);
    connect(m_pane, &WorkspacePaneViewModel::activeTabChanged, this, &WorkspacePaneWidget::onActiveTabChanged);

    for (int i = 0; i < m_pane->tabCount(); ++i)
    {
        addPageForTab(m_pane->tabAt(i), i);
    }

    if (TabViewModel* active = m_pane->activeTab())
    {
        const int index = indexOfTab(active);
        if (index >= 0)
        {
            m_tabWidget->setCurrentIndex(index);
        }
        bindToolBarToTab(active);
    }
}

void WorkspacePaneWidget::createViewModeActions()
{
    m_viewModeActionGroup = new QActionGroup(this);
    m_viewModeActionGroup->setExclusive(true);

    for (ViewMode mode : kViewModes)
    {
        QAction* action = new QAction(viewModeLabel(mode), this);
        action->setCheckable(true);
        m_viewModeActionGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode]() {
            if (m_boundTab)
            {
                m_boundTab->setViewMode(mode);
            }
        });
        m_viewModeActions.append(action);
    }
}

QToolBar* WorkspacePaneWidget::createToolBar()
{
    auto* toolBar = new QToolBar(this);
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

    connect(m_backAction, &QAction::triggered, this, [this]() { if (m_boundTab) m_boundTab->goBack(); });
    connect(m_forwardAction, &QAction::triggered, this, [this]() { if (m_boundTab) m_boundTab->goForward(); });
    connect(m_upAction, &QAction::triggered, this, [this]() { if (m_boundTab) m_boundTab->goUp(); });
    connect(m_addressBar, &QLineEdit::returnPressed, this, &WorkspacePaneWidget::onAddressBarEdited);

    return toolBar;
}

void WorkspacePaneWidget::createTabArea()
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(false);

    auto* newTabButton = new QToolButton(m_tabWidget);
    newTabButton->setText(QStringLiteral("+"));
    newTabButton->setToolTip(tr("New tab"));
    connect(newTabButton, &QToolButton::clicked, this, &WorkspacePaneWidget::onNewTabRequested);
    m_tabWidget->setCornerWidget(newTabButton, Qt::TopRightCorner);

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &WorkspacePaneWidget::onTabWidgetCurrentChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, m_pane, &WorkspacePaneViewModel::closeTab);
}

void WorkspacePaneWidget::addPageForTab(TabViewModel* tab, int index)
{
    auto* browserView = new FileBrowserView(tab->fileListModel(), m_tabWidget);
    browserView->setViewMode(tab->viewMode());

    m_tabWidget->insertTab(index, browserView, tabLabelFor(tab->currentPath()));

    connect(tab, &TabViewModel::currentPathChanged, this, [this, tab](const std::filesystem::path& path) {
        const int idx = indexOfTab(tab);
        if (idx >= 0)
        {
            m_tabWidget->setTabText(idx, tabLabelFor(path));
        }
    });

    connect(browserView, &FileBrowserView::itemActivated, tab, [this, tab](const std::filesystem::path& path, bool isDirectory) {
        if (isDirectory)
        {
            tab->navigateTo(path);
        }
        else
        {
            m_fileOperationsController->openFile(path);
        }
    });

    // 1:1 per tab (unlike the toolbar rebinding below, which follows only the active tab) so a
    // background tab's selection doesn't leak into another tab's, and is preserved when revisited.
    connect(browserView, &FileBrowserView::selectionChanged, tab, &TabViewModel::setSelectedEntry);

    connect(browserView, &FileBrowserView::copyRequested, tab, [this, tab]() {
        if (tab->selectedEntry())
        {
            m_fileOperationsController->copyToClipboard(tab->selectedEntry()->path());
        }
    });
    connect(browserView, &FileBrowserView::cutRequested, tab, [this, tab]() {
        if (tab->selectedEntry())
        {
            m_fileOperationsController->cutToClipboard(tab->selectedEntry()->path());
        }
    });
    connect(browserView, &FileBrowserView::pasteRequested, tab, [this, tab]() {
        m_fileOperationsController->pasteInto(tab->currentPath());
    });
    connect(browserView, &FileBrowserView::deleteRequested, tab, [this, tab](bool permanent) {
        onDeleteRequested(tab, permanent);
    });
    connect(browserView, &FileBrowserView::navigateUpRequested, tab, [tab]() { tab->goUp(); });

    connect(browserView, &FileBrowserView::itemContextMenuRequested, tab,
            [this, tab](const std::vector<std::filesystem::path>& paths, const QPoint& globalPos) {
                showItemContextMenu(tab, paths, globalPos);
            });
    connect(browserView, &FileBrowserView::folderContextMenuRequested, tab, [this, tab](const QPoint& globalPos) {
        showBackgroundContextMenu(tab, globalPos);
    });
}

void WorkspacePaneWidget::bindToolBarToTab(TabViewModel* tab)
{
    if (m_boundTab)
    {
        disconnect(m_boundTab, &TabViewModel::currentPathChanged, this, &WorkspacePaneWidget::onCurrentPathChanged);
        disconnect(m_boundTab, &TabViewModel::backAvailableChanged, m_backAction, &QAction::setEnabled);
        disconnect(m_boundTab, &TabViewModel::forwardAvailableChanged, m_forwardAction, &QAction::setEnabled);
        disconnect(m_boundTab, &TabViewModel::upAvailableChanged, m_upAction, &QAction::setEnabled);
        disconnect(m_boundTab, &TabViewModel::navigationFailed, this, &WorkspacePaneWidget::onNavigationFailed);
        disconnect(m_boundTab, &TabViewModel::viewModeChanged, this, &WorkspacePaneWidget::onViewModeChanged);
    }

    m_boundTab = tab;

    if (!m_boundTab)
    {
        m_backAction->setEnabled(false);
        m_forwardAction->setEnabled(false);
        m_upAction->setEnabled(false);
        m_addressBar->clear();
        return;
    }

    connect(m_boundTab, &TabViewModel::currentPathChanged, this, &WorkspacePaneWidget::onCurrentPathChanged);
    connect(m_boundTab, &TabViewModel::backAvailableChanged, m_backAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::forwardAvailableChanged, m_forwardAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::upAvailableChanged, m_upAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::navigationFailed, this, &WorkspacePaneWidget::onNavigationFailed);
    connect(m_boundTab, &TabViewModel::viewModeChanged, this, &WorkspacePaneWidget::onViewModeChanged);

    m_addressBar->setText(toQString(m_boundTab->currentPath()));
    onViewModeChanged(m_boundTab->viewMode());
}

int WorkspacePaneWidget::indexOfTab(TabViewModel* tab) const
{
    for (int i = 0; i < m_pane->tabCount(); ++i)
    {
        if (m_pane->tabAt(i) == tab)
        {
            return i;
        }
    }
    return -1;
}

void WorkspacePaneWidget::onTabAdded(int index)
{
    addPageForTab(m_pane->tabAt(index), index);
}

void WorkspacePaneWidget::onTabClosed(int index)
{
    QWidget* page = m_tabWidget->widget(index);
    m_tabWidget->removeTab(index);
    delete page;
}

void WorkspacePaneWidget::onActiveTabChanged(int index)
{
    if (m_tabWidget->currentIndex() != index)
    {
        m_tabWidget->setCurrentIndex(index);
    }
    bindToolBarToTab(m_pane->tabAt(index));
}

void WorkspacePaneWidget::onTabWidgetCurrentChanged(int index)
{
    if (index < 0)
    {
        return;
    }
    m_pane->setActiveTab(index);
}

void WorkspacePaneWidget::onNewTabRequested()
{
    const auto seedPath = m_pane->activeTab() ? m_pane->activeTab()->currentPath() : std::filesystem::path();

    TabViewModel* tab = m_pane->addTab();
    if (!seedPath.empty())
    {
        tab->navigateTo(seedPath);
    }
}

void WorkspacePaneWidget::onCurrentPathChanged(const std::filesystem::path& path)
{
    m_addressBar->setText(toQString(path));
}

void WorkspacePaneWidget::onAddressBarEdited()
{
    if (m_boundTab)
    {
        m_boundTab->navigateTo(std::filesystem::path(m_addressBar->text().toStdWString()));
    }
}

void WorkspacePaneWidget::onNavigationFailed(const std::filesystem::path& path, const QString& message)
{
    Q_UNUSED(path);

    if (m_boundTab)
    {
        m_addressBar->setText(toQString(m_boundTab->currentPath()));
    }

    emit navigationFailed(message);
}

void WorkspacePaneWidget::onDeleteRequested(TabViewModel* tab, bool permanent)
{
    if (!tab->selectedEntry())
    {
        return;
    }

    const FileNode entry = *tab->selectedEntry();
    const QString name = toQString(entry.name());

    const QMessageBox::StandardButton answer = permanent
        ? QMessageBox::question(this, tr("Delete Permanently"),
                                 tr("Permanently delete \"%1\"? This cannot be undone.").arg(name))
        : QMessageBox::question(this, tr("Delete"), tr("Move \"%1\" to the Recycle Bin?").arg(name));

    if (answer != QMessageBox::Yes)
    {
        return;
    }

    if (permanent)
    {
        m_fileOperationsController->deletePermanently(entry.path());
    }
    else
    {
        m_fileOperationsController->moveToTrash(entry.path());
    }
}

void WorkspacePaneWidget::onViewModeChanged(ViewMode mode)
{
    if (QWidget* page = m_tabWidget->currentWidget())
    {
        static_cast<FileBrowserView*>(page)->setViewMode(mode);
    }

    const auto it = std::find(kViewModes.begin(), kViewModes.end(), mode);
    if (it == kViewModes.end())
    {
        return;
    }

    const auto index = std::distance(kViewModes.begin(), it);
    m_viewModeActions[static_cast<int>(index)]->setChecked(true);
}

bool WorkspacePaneWidget::extendedShellExtensionsEnabled()
{
    QSettings settings;
    return settings.value(QLatin1String(kShowShellExtensionsSettingsKey), false).toBool();
}

void WorkspacePaneWidget::showItemContextMenu(TabViewModel* tab, const std::vector<std::filesystem::path>& paths,
                                               const QPoint& globalPos)
{
    if (paths.empty())
    {
        return;
    }

    const auto ownerWindow = reinterpret_cast<NativeWindowHandle>(window()->winId());
    const std::filesystem::path directory = paths.front().parent_path();
    const std::filesystem::path targetPath = paths.front();

    auto* openAction = new QAction(tr("Open"));
    auto* cutAction = new QAction(tr("Cut"));
    auto* copyAction = new QAction(tr("Copy"));
    auto* pasteAction = new QAction(tr("Paste"));
    auto* deleteAction = new QAction(tr("Delete"));
    auto* renameAction = new QAction(tr("Rename"));
    auto* propertiesAction = new QAction(tr("Properties"));

    connect(openAction, &QAction::triggered, this, [this, targetPath]() { m_fileOperationsController->openFile(targetPath); });
    connect(cutAction, &QAction::triggered, this, [this, targetPath]() { m_fileOperationsController->cutToClipboard(targetPath); });
    connect(copyAction, &QAction::triggered, this, [this, targetPath]() { m_fileOperationsController->copyToClipboard(targetPath); });
    connect(pasteAction, &QAction::triggered, this, [this, directory]() { m_fileOperationsController->pasteInto(directory); });
    connect(deleteAction, &QAction::triggered, this, [this, tab]() { onDeleteRequested(tab, false); });
    connect(renameAction, &QAction::triggered, this, [this, targetPath]() { promptRename(targetPath); });
    connect(propertiesAction, &QAction::triggered, this, [this, targetPath, ownerWindow]() {
        m_fileOperationsController->showProperties(targetPath, ownerWindow);
    });

    ContextMenuBuilder::NativeActions actions;
    actions.open = openAction;
    actions.cut = cutAction;
    actions.copy = copyAction;
    actions.paste = pasteAction;
    actions.deleteAction = deleteAction;
    actions.rename = renameAction;
    actions.properties = propertiesAction;

    const ContextMenuSourceMode mode =
        extendedShellExtensionsEnabled() ? ContextMenuSourceMode::StaticAndShellExtensions : ContextMenuSourceMode::StaticVerbsOnly;
    auto buildResult = m_fileOperationsController->buildContextMenuForSelection(paths, mode);

    std::vector<ContextMenuEntry> entries;
    if (buildResult.hasValue())
    {
        entries = std::move(buildResult).value();
    }
    else
    {
        emit navigationFailed(QString::fromStdString(buildResult.error().message));
    }

    QMenu* menu = ContextMenuBuilder::buildItemMenu(entries, actions, this);
    QAction* chosen = menu->exec(globalPos);

    const auto entryId = chosen ? ContextMenuBuilder::entryIdForAction(chosen) : std::nullopt;
    if (entryId)
    {
        m_fileOperationsController->invokeContextMenuEntry(*entryId, directory, ownerWindow);
    }
    else
    {
        m_fileOperationsController->discardContextMenu();
    }

    menu->deleteLater();
    for (QAction* action : { openAction, cutAction, copyAction, pasteAction, deleteAction, renameAction, propertiesAction })
    {
        action->deleteLater();
    }
}

void WorkspacePaneWidget::showBackgroundContextMenu(TabViewModel* tab, const QPoint& globalPos)
{
    const std::filesystem::path directory = tab->currentPath();
    const auto ownerWindow = reinterpret_cast<NativeWindowHandle>(window()->winId());

    auto* pasteAction = new QAction(tr("Paste"));
    auto* newFolderAction = new QAction(tr("New Folder"));
    auto* propertiesAction = new QAction(tr("Properties"));

    connect(pasteAction, &QAction::triggered, this, [this, directory]() { m_fileOperationsController->pasteInto(directory); });
    connect(newFolderAction, &QAction::triggered, this, [this, directory]() {
        if (auto createdPath = m_fileOperationsController->createFolder(directory))
        {
            promptRename(*createdPath);
        }
    });
    connect(propertiesAction, &QAction::triggered, this, [this, directory, ownerWindow]() {
        m_fileOperationsController->showProperties(directory, ownerWindow);
    });

    ContextMenuBuilder::NativeActions actions;
    actions.paste = pasteAction;
    actions.newFolder = newFolderAction;
    actions.properties = propertiesAction;

    const ContextMenuSourceMode mode =
        extendedShellExtensionsEnabled() ? ContextMenuSourceMode::StaticAndShellExtensions : ContextMenuSourceMode::StaticVerbsOnly;
    auto buildResult = m_fileOperationsController->buildContextMenuForFolder(directory, mode);

    std::vector<ContextMenuEntry> entries;
    if (buildResult.hasValue())
    {
        entries = std::move(buildResult).value();
    }
    else
    {
        emit navigationFailed(QString::fromStdString(buildResult.error().message));
    }

    QMenu* menu = ContextMenuBuilder::buildBackgroundMenu(entries, actions, this);
    QAction* chosen = menu->exec(globalPos);

    const auto entryId = chosen ? ContextMenuBuilder::entryIdForAction(chosen) : std::nullopt;
    if (entryId)
    {
        m_fileOperationsController->invokeContextMenuEntry(*entryId, directory, ownerWindow);
    }
    else
    {
        m_fileOperationsController->discardContextMenu();
    }

    menu->deleteLater();
    for (QAction* action : { pasteAction, newFolderAction, propertiesAction })
    {
        action->deleteLater();
    }
}

void WorkspacePaneWidget::promptRename(const std::filesystem::path& path)
{
    const QString currentName = toQString(path.filename());

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, currentName, &ok);
    if (!ok || newName.isEmpty() || newName == currentName)
    {
        return;
    }

    const std::filesystem::path destination = path.parent_path() / std::filesystem::path(newName.toStdWString());
    m_fileOperationsController->renamePath(path, destination);
}
