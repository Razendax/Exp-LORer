#include "WorkspacePaneWidget.h"

#include <algorithm>
#include <array>

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "AddressBarWidget.h"
#include "BottomPanelWidget.h"
#include "ContextMenuBuilder.h"
#include "FileBrowserView.h"
#include "FileOperationsController.h"
#include "IContextMenuProvider.h"
#include "SearchCriteriaPanel.h"
#include "SearchResultsPane.h"
#include "StatusBarWidget.h"
#include "TabViewModel.h"
#include "TerminalWidget.h"
#include "UiColors.h"
#include "VirtualPaths.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // "This PC" rather than its raw sentinel path string, everywhere a path is shown to the user.
    QString displayPathText(const std::filesystem::path& path)
    {
        if (path == VirtualPaths::ThisPC)
        {
            return QObject::tr("This PC");
        }
        return toQString(path);
    }

    QString tabLabelFor(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return QObject::tr("New tab");
        }

        if (path == VirtualPaths::ThisPC)
        {
            return QObject::tr("This PC");
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
                                          const std::array<int, FileListModel::ColumnCount>& initialColumnWidths,
                                          QWidget* parent)
    : QWidget(parent)
    , m_pane(pane)
    , m_fileOperationsController(fileOperationsController)
    , m_initialColumnWidths(initialColumnWidths)
{
    createViewModeActions();
    QToolBar* toolBar = createToolBar();
    createTabArea();

    m_statusBar = new StatusBarWidget(this);
    connect(m_statusBar, &StatusBarWidget::quickSelectTextChanged, this, &WorkspacePaneWidget::onQuickSelectTextChanged);
    connect(m_statusBar, &StatusBarWidget::quickSelectCancelled, this, [this]() {
        if (auto* browserView = currentBrowserView())
        {
            browserView->focusView();
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolBar);
    layout->addWidget(m_tabWidget);
    layout->addWidget(m_statusBar);

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

    m_toolBar = toolBar;
    toolBar->setObjectName(QStringLiteral("paneToolBar"));
    toolBar->setAttribute(Qt::WA_StyledBackground, true);
    toolBar->setProperty("paneActive", false);
    toolBar->setStyleSheet(QStringLiteral("QToolBar#paneToolBar { background: %1; } "
                                           "QToolBar#paneToolBar[paneActive=\"true\"] { background: %2; }")
                                .arg(QLatin1String(UiColors::kNavigationPanelBackground),
                                     QLatin1String(UiColors::kActivePaneAccentBackground)));

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

    m_addressBar = new AddressBarWidget(toolBar);
    m_addressBar->setClearButtonEnabled(true);
    toolBar->addWidget(m_addressBar);

    connect(m_backAction, &QAction::triggered, this, [this]() { if (m_boundTab) m_boundTab->goBack(); });
    connect(m_forwardAction, &QAction::triggered, this, [this]() { if (m_boundTab) m_boundTab->goForward(); });
    connect(m_upAction, &QAction::triggered, this, [this]() { if (m_boundTab) m_boundTab->goUp(); });
    connect(m_addressBar, &QLineEdit::returnPressed, this, &WorkspacePaneWidget::onAddressBarEdited);
    connect(m_addressBar, &AddressBarWidget::folderSuggestionsRequested, this,
            &WorkspacePaneWidget::onAddressBarFolderSuggestionsRequested);

    return toolBar;
}

void WorkspacePaneWidget::createTabArea()
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("paneTabWidget"));
    m_tabWidget->setStyleSheet(QStringLiteral(
        // The selected-tab rule below adds a 2px bottom border; giving every tab (not just the
        // selected one) that same border width -- transparent when unselected -- reserves the
        // space up front so switching tabs doesn't change tab height/width and jitter the tab bar.
        "QTabWidget#paneTabWidget::pane { border-top: 1px solid palette(mid); }"
        "QTabWidget#paneTabWidget QTabBar::tab { border-bottom: 2px solid transparent; min-width: 80px; max-width: 220px; }"
        "QTabWidget#paneTabWidget QTabBar::tab:selected { background: %1; border-bottom: 2px solid %2; }")
        .arg(QLatin1String(UiColors::kActiveTabBackground), QLatin1String(UiColors::kAccentBlue)));
    m_tabWidget->setTabsClosable(tabCloseButtonsEnabled());
    m_tabWidget->setMovable(false);
    m_tabWidget->tabBar()->installEventFilter(this);

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
    auto* stack = new QStackedWidget(m_tabWidget);

    auto* browserView = new FileBrowserView(tab->fileListModel(), stack);
    browserView->setViewMode(tab->viewMode());
    browserView->setColumnWidths(m_initialColumnWidths);
    wireBrowserView(browserView, tab);
    stack->addWidget(browserView); // page 0: normal browsing

    auto* searchResultsView = new FileBrowserView(tab->searchResultsModel(), stack);
    searchResultsView->setViewMode(tab->viewMode());
    searchResultsView->setColumnWidths(m_initialColumnWidths);
    wireBrowserView(searchResultsView, tab);
    stack->addWidget(searchResultsView); // page 1: search results

    auto* advancedSearchPane = new SearchResultsPane(tab->advancedSearchResultsModel(), stack);
    advancedSearchPane->setCriteria(tab->advancedSearchCriteria(), tab->resolveTagCriteria());
    advancedSearchPane->browserView()->setColumnWidths(m_initialColumnWidths);
    wireBrowserView(advancedSearchPane->browserView(), tab);
    stack->addWidget(advancedSearchPane); // page 2: advanced search results

    connect(tab, &TabViewModel::currentPathChanged, this, [this, tab](const std::filesystem::path& path) {
        const int idx = indexOfTab(tab);
        if (idx >= 0)
        {
            m_tabWidget->setTabText(idx, tabLabelFor(path));
        }
    });

    auto pickStackPage = [stack, tab]() {
        if (tab->advancedSearchActive())
        {
            stack->setCurrentIndex(2);
        }
        else if (tab->searchActive())
        {
            stack->setCurrentIndex(1);
        }
        else
        {
            stack->setCurrentIndex(0);
        }
    };
    connect(tab, &TabViewModel::searchModeChanged, stack, [pickStackPage](bool) { pickStackPage(); });
    connect(tab, &TabViewModel::advancedSearchModeChanged, stack, [pickStackPage](bool) { pickStackPage(); });

    connect(advancedSearchPane->criteriaPanel(), &SearchCriteriaPanel::searchRequested, tab,
            [tab](const SearchCriteria& criteria) { tab->startAdvancedSearch(criteria); });
    connect(advancedSearchPane->criteriaPanel(), &SearchCriteriaPanel::criteriaEdited, tab,
            [tab](const SearchCriteria& criteria) { tab->updateAdvancedSearchCriteria(criteria); });
    connect(advancedSearchPane->criteriaPanel(), &SearchCriteriaPanel::closeRequested, tab,
            [tab]() { tab->exitAdvancedSearch(); });

    connect(advancedSearchPane->criteriaPanel(), &SearchCriteriaPanel::tagSearchQueryRequested, tab,
            [advancedSearchPane = advancedSearchPane, tab](const QString& query) {
                advancedSearchPane->criteriaPanel()->setTagSearchResults(tab->searchTagsForCriteria(query));
            });
    connect(advancedSearchPane->criteriaPanel(), &SearchCriteriaPanel::tagCriterionAdded, tab,
            [tab](Tag::Id id) { tab->addTagSearchCriterion(id); });
    connect(advancedSearchPane->criteriaPanel(), &SearchCriteriaPanel::tagCriterionRemoved, tab,
            [tab](Tag::Id id) { tab->removeTagSearchCriterion(id); });

    connect(tab, &TabViewModel::advancedSearchResultsChanged, advancedSearchPane,
            [advancedSearchPane, tab](const std::vector<FileNode>&) {
                advancedSearchPane->setHighlightQuery(QString::fromStdString(tab->advancedSearchCriteria().nameQuery));
            });

    connect(tab, &TabViewModel::advancedSearchCriteriaChanged, advancedSearchPane,
            [advancedSearchPane, tab](const SearchCriteria& criteria) {
                advancedSearchPane->setCriteria(criteria, tab->resolveTagCriteria());
            });

    // Bottom panel (Architecture.md §14.23): a splitter over the existing browse/search/
    // advanced-search stack and a collapsible BottomPanelWidget with a single "Terminal" tab, wrapped
    // as the actual tab page instead of inserting `stack` directly.
    auto* bottomPanel = new BottomPanelWidget(m_tabWidget);
    auto* terminalWidget = new TerminalWidget(bottomPanel);
    bottomPanel->addPanelTab(tr("Terminal"), terminalWidget);

    auto* splitter = new QSplitter(Qt::Vertical, m_tabWidget);
    splitter->addWidget(stack);
    splitter->addWidget(bottomPanel);
    splitter->setCollapsible(0, false);
    splitter->setSizes({ 1000, bottomPanel->collapsedHeight() });

    connect(bottomPanel, &BottomPanelWidget::expansionChanged, splitter, [splitter, bottomPanel](bool expanded) {
        const int total = std::max(splitter->height(), splitter->sizeHint().height());
        const int bottomHeight = expanded ? bottomPanel->preferredExpandedHeight() : bottomPanel->collapsedHeight();
        splitter->setSizes({ total - bottomHeight, bottomHeight });
    });

    connect(bottomPanel, &BottomPanelWidget::panelTabFirstActivated, terminalWidget, [terminalWidget, tab](int) {
        auto cwd = tab->currentPath();
        if (cwd == VirtualPaths::ThisPC || cwd.empty())
        {
            cwd = std::filesystem::path(QDir::homePath().toStdWString());
        }
        terminalWidget->start(cwd);
    });

    connect(terminalWidget, &TerminalWidget::restartRequested, terminalWidget, [terminalWidget, tab]() {
        auto cwd = tab->currentPath();
        if (cwd == VirtualPaths::ThisPC || cwd.empty())
        {
            cwd = std::filesystem::path(QDir::homePath().toStdWString());
        }
        terminalWidget->start(cwd);
    });

    m_tabWidget->insertTab(index, splitter, tabLabelFor(tab->currentPath()));
}

void WorkspacePaneWidget::wireBrowserView(FileBrowserView* browserView, TabViewModel* tab)
{
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
    connect(browserView, &FileBrowserView::selectionChanged, tab, &TabViewModel::setSelectedEntries);

    connect(browserView, &FileBrowserView::copyRequested, tab, [this, tab]() {
        if (tab->selectedEntries().empty())
        {
            return;
        }
        std::vector<std::filesystem::path> paths;
        for (const FileNode& entry : tab->selectedEntries())
        {
            paths.push_back(entry.path());
        }
        m_fileOperationsController->copyToClipboard(paths);
    });
    connect(browserView, &FileBrowserView::cutRequested, tab, [this, tab]() {
        if (tab->selectedEntries().empty())
        {
            return;
        }
        std::vector<std::filesystem::path> paths;
        for (const FileNode& entry : tab->selectedEntries())
        {
            paths.push_back(entry.path());
        }
        m_fileOperationsController->cutToClipboard(paths);
    });
    connect(browserView, &FileBrowserView::pasteRequested, tab, [this, tab]() {
        m_fileOperationsController->pasteInto(tab->currentPath());
    });
    connect(browserView, &FileBrowserView::deleteRequested, tab, [this, tab](bool permanent) {
        onDeleteRequested(tab, permanent);
    });
    connect(browserView, &FileBrowserView::navigateUpRequested, tab, [tab]() { tab->goUp(); });
    connect(browserView, &FileBrowserView::navigateBackRequested, tab, [tab]() { tab->goBack(); });
    connect(browserView, &FileBrowserView::navigateForwardRequested, tab, [tab]() { tab->goForward(); });

    connect(browserView, &FileBrowserView::renameRequested, tab,
            [this](const std::filesystem::path& source, const std::filesystem::path& destination) {
                m_fileOperationsController->renamePath(source, destination);
            });

    connect(browserView, &FileBrowserView::itemContextMenuRequested, tab,
            [this, tab, browserView](const std::vector<std::filesystem::path>& paths, const QPoint& globalPos) {
                showItemContextMenu(tab, browserView, paths, globalPos);
            });
    connect(browserView, &FileBrowserView::folderContextMenuRequested, tab, [this, tab, browserView](const QPoint& globalPos) {
        showBackgroundContextMenu(tab, browserView, globalPos);
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

        disconnect(m_boundTab, &TabViewModel::directoryContentsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::searchResultsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::advancedSearchResultsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::searchModeChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::advancedSearchModeChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::selectedEntriesChanged, this, &WorkspacePaneWidget::refreshStatusCounts);

        disconnect(m_boundTab, &TabViewModel::currentPathChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);
        disconnect(m_boundTab, &TabViewModel::searchModeChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);
        disconnect(m_boundTab, &TabViewModel::advancedSearchModeChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);
    }

    m_boundTab = tab;

    if (!m_boundTab)
    {
        m_backAction->setEnabled(false);
        m_forwardAction->setEnabled(false);
        m_upAction->setEnabled(false);
        m_addressBar->clear();
        m_statusBar->setCounts(0, 0);
        m_statusBar->clearQuickSelect();
        return;
    }

    connect(m_boundTab, &TabViewModel::currentPathChanged, this, &WorkspacePaneWidget::onCurrentPathChanged);
    connect(m_boundTab, &TabViewModel::backAvailableChanged, m_backAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::forwardAvailableChanged, m_forwardAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::upAvailableChanged, m_upAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::navigationFailed, this, &WorkspacePaneWidget::onNavigationFailed);
    connect(m_boundTab, &TabViewModel::viewModeChanged, this, &WorkspacePaneWidget::onViewModeChanged);

    connect(m_boundTab, &TabViewModel::directoryContentsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::searchResultsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::advancedSearchResultsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::searchModeChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::advancedSearchModeChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::selectedEntriesChanged, this, &WorkspacePaneWidget::refreshStatusCounts);

    connect(m_boundTab, &TabViewModel::currentPathChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);
    connect(m_boundTab, &TabViewModel::searchModeChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);
    connect(m_boundTab, &TabViewModel::advancedSearchModeChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);

    // Sync against a tab that already navigated before this binding existed (e.g. tabs restored
    // from the saved workspace config navigate before MainWindow/WorkspacePaneWidget are
    // constructed) -- the *Changed signals above only fire on future navigation, not retroactively.
    m_backAction->setEnabled(m_boundTab->backAvailable());
    m_forwardAction->setEnabled(m_boundTab->forwardAvailable());
    m_upAction->setEnabled(m_boundTab->upAvailable());

    m_addressBar->setText(displayPathText(m_boundTab->currentPath()));
    m_statusBar->clearQuickSelect();
    onViewModeChanged(m_boundTab->viewMode());
    refreshStatusCounts();
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
    m_addressBar->setText(displayPathText(path));
}

void WorkspacePaneWidget::onAddressBarEdited()
{
    if (!m_boundTab)
    {
        return;
    }

    const QString trimmed = m_addressBar->text().trimmed();
    if (trimmed.compare(QStringLiteral("this pc"), Qt::CaseInsensitive) == 0)
    {
        m_boundTab->navigateTo(VirtualPaths::ThisPC);
        return;
    }

    std::filesystem::path newPath(trimmed.toStdWString());
    newPath.make_preferred();
    m_boundTab->navigateTo(newPath);
}

void WorkspacePaneWidget::onAddressBarFolderSuggestionsRequested(const std::filesystem::path& directory)
{
    if (!m_boundTab)
    {
        return;
    }

    m_addressBar->setSuggestions(directory, m_boundTab->suggestFolders(directory));
}

void WorkspacePaneWidget::onNavigationFailed(const std::filesystem::path& path, const QString& message)
{
    Q_UNUSED(path);

    if (m_boundTab)
    {
        m_addressBar->setText(displayPathText(m_boundTab->currentPath()));
    }

    emit navigationFailed(message);
}

void WorkspacePaneWidget::onDeleteRequested(TabViewModel* tab, bool permanent)
{
    const std::vector<FileNode>& entries = tab->selectedEntries();
    if (entries.empty())
    {
        return;
    }

    const QString message = [&]() {
        if (entries.size() == 1)
        {
            const QString name = toQString(entries.front().name());
            return permanent ? tr("Permanently delete \"%1\"? This cannot be undone.").arg(name)
                              : tr("Move \"%1\" to the Recycle Bin?").arg(name);
        }
        return permanent ? tr("Permanently delete %1 items? This cannot be undone.").arg(entries.size())
                          : tr("Move %1 items to the Recycle Bin?").arg(entries.size());
    }();

    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, permanent ? tr("Delete Permanently") : tr("Delete"), message);

    if (answer != QMessageBox::Yes)
    {
        return;
    }

    for (const FileNode& entry : entries)
    {
        if (permanent)
        {
            m_fileOperationsController->deletePermanently(entry.path());
        }
        else
        {
            m_fileOperationsController->moveToTrash(entry.path());
        }
    }
}

void WorkspacePaneWidget::onViewModeChanged(ViewMode mode)
{
    // The tab page is now a QSplitter (browse/search/advanced-search stack + BottomPanelWidget,
    // Architecture.md §14.23); the stack is always its first direct child (bottomPanel, the
    // splitter's other child, is never itself a QStackedWidget), so this unwrap stays unambiguous.
    auto* splitter = qobject_cast<QSplitter*>(m_tabWidget->currentWidget());
    if (auto* stack = splitter ? qobject_cast<QStackedWidget*>(splitter->widget(0)) : nullptr)
    {
        for (int i = 0; i < stack->count(); ++i)
        {
            // The per-tab stack is heterogeneous: pages 0/1 are plain FileBrowserView, page 2 is a
            // SearchResultsPane wrapping one. A blind static_cast<FileBrowserView*> on page 2 would
            // reinterpret a SearchResultsPane's memory as a FileBrowserView, corrupting/crashing.
            if (auto* browserView = qobject_cast<FileBrowserView*>(stack->widget(i)))
            {
                browserView->setViewMode(mode);
            }
            else if (auto* searchPane = qobject_cast<SearchResultsPane*>(stack->widget(i)))
            {
                searchPane->browserView()->setViewMode(mode);
            }
        }
    }

    const auto it = std::find(kViewModes.begin(), kViewModes.end(), mode);
    if (it == kViewModes.end())
    {
        return;
    }

    const auto index = std::distance(kViewModes.begin(), it);
    m_viewModeActions[static_cast<int>(index)]->setChecked(true);
}

std::array<int, FileListModel::ColumnCount> WorkspacePaneWidget::currentColumnWidths() const
{
    if (auto* browserView = currentBrowserView())
    {
        return browserView->columnWidths();
    }

    return m_initialColumnWidths;
}

FileBrowserView* WorkspacePaneWidget::currentBrowserView() const
{
    // Same splitter-unwrap and heterogeneous-stack shape as onViewModeChanged: page 2 is a
    // SearchResultsPane wrapping a FileBrowserView, pages 0/1 are plain FileBrowserView.
    auto* splitter = qobject_cast<QSplitter*>(m_tabWidget->currentWidget());
    if (auto* stack = splitter ? qobject_cast<QStackedWidget*>(splitter->widget(0)) : nullptr)
    {
        if (auto* browserView = qobject_cast<FileBrowserView*>(stack->currentWidget()))
        {
            return browserView;
        }
        if (auto* searchPane = qobject_cast<SearchResultsPane*>(stack->currentWidget()))
        {
            return searchPane->browserView();
        }
    }

    return nullptr;
}

void WorkspacePaneWidget::refreshStatusCounts()
{
    if (!m_boundTab)
    {
        return;
    }

    const int total = m_boundTab->advancedSearchActive() ? m_boundTab->advancedSearchResultsModel()->rowCount()
                     : m_boundTab->searchActive()          ? m_boundTab->searchResultsModel()->rowCount()
                                                            : m_boundTab->fileListModel()->rowCount();
    const int selected = static_cast<int>(m_boundTab->selectedEntries().size());
    m_statusBar->setCounts(total, selected);
}

void WorkspacePaneWidget::onQuickSelectTextChanged(const QString& text)
{
    if (auto* browserView = currentBrowserView())
    {
        browserView->selectEntriesContaining(text);
    }
}

bool WorkspacePaneWidget::extendedShellExtensionsEnabled()
{
    QSettings settings;
    return settings.value(QLatin1String(kShowShellExtensionsSettingsKey), false).toBool();
}

bool WorkspacePaneWidget::tabCloseButtonsEnabled()
{
    QSettings settings;
    return settings.value(QLatin1String(kShowTabCloseButtonsSettingsKey), true).toBool();
}

void WorkspacePaneWidget::setTabCloseButtonsVisible(bool visible)
{
    m_tabWidget->setTabsClosable(visible);
}

void WorkspacePaneWidget::setActive(bool active)
{
    m_toolBar->setProperty("paneActive", active);
    m_toolBar->style()->unpolish(m_toolBar);
    m_toolBar->style()->polish(m_toolBar);
    m_toolBar->update();
}

bool WorkspacePaneWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_tabWidget->tabBar() && event->type() == QEvent::MouseButtonRelease)
    {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::MiddleButton)
        {
            const int index = m_tabWidget->tabBar()->tabAt(mouseEvent->pos());
            if (index >= 0)
            {
                m_pane->closeTab(index);
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void WorkspacePaneWidget::showItemContextMenu(TabViewModel* tab, FileBrowserView* browserView,
                                               const std::vector<std::filesystem::path>& paths, const QPoint& globalPos)
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
    connect(cutAction, &QAction::triggered, this, [this, paths]() { m_fileOperationsController->cutToClipboard(paths); });
    connect(copyAction, &QAction::triggered, this, [this, paths]() { m_fileOperationsController->copyToClipboard(paths); });
    connect(pasteAction, &QAction::triggered, this, [this, directory]() { m_fileOperationsController->pasteInto(directory); });
    connect(deleteAction, &QAction::triggered, this, [this, tab]() { onDeleteRequested(tab, false); });
    connect(renameAction, &QAction::triggered, this, [browserView, targetPath]() { browserView->beginRename(targetPath); });
    connect(propertiesAction, &QAction::triggered, this, [this, targetPath, ownerWindow]() {
        m_fileOperationsController->showProperties(targetPath, ownerWindow);
    });

    // Rename/Properties/Open are single-target-only (rename-as-move, showProperties, and
    // openFile/itemActivated all take one path) — disable rather than guess a multi-item
    // behavior when more than one item is selected (Architecture.md §14.13.2/§14.13.6).
    const bool singleTarget = paths.size() == 1;
    openAction->setEnabled(singleTarget);
    renameAction->setEnabled(singleTarget);
    propertiesAction->setEnabled(singleTarget);

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

void WorkspacePaneWidget::showBackgroundContextMenu(TabViewModel* tab, FileBrowserView* browserView, const QPoint& globalPos)
{
    const std::filesystem::path directory = tab->currentPath();
    if (directory == VirtualPaths::ThisPC)
    {
        return;
    }

    const auto ownerWindow = reinterpret_cast<NativeWindowHandle>(window()->winId());

    auto* pasteAction = new QAction(tr("Paste"));
    auto* newFolderAction = new QAction(tr("New Folder"));
    auto* propertiesAction = new QAction(tr("Properties"));

    connect(pasteAction, &QAction::triggered, this, [this, directory]() { m_fileOperationsController->pasteInto(directory); });
    connect(newFolderAction, &QAction::triggered, this, [this, directory, browserView]() {
        if (auto createdPath = m_fileOperationsController->createFolder(directory))
        {
            browserView->beginRename(*createdPath);
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
