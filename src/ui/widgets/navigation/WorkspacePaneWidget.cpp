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
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "AddressBarWidget.h"
#include "ArchiveExtensions.h"
#include "BottomPanelWidget.h"
#include "ColumnVisibilitySettingsViewModel.h"
#include "ContextMenuBuilder.h"
#include "FileBrowserView.h"
#include "FileItemContextMenu.h"
#include "FileOperationsController.h"
#include "FolderSizeWidget.h"
#include "FragmentationWidget.h"
#include "IContextMenuProvider.h"
#include "MediaViewerSortViewBar.h"
#include "PathContainment.h"
#include "SearchCriteriaPanel.h"
#include "SearchResultsPane.h"
#include "StatusBarWidget.h"
#include "TabViewModel.h"
#include "TerminalSessionsWidget.h"
#include "TerminalWidget.h"
#include "UiColors.h"
#include "UiThemeChangeNotifier.h"
#include "VirtualPaths.h"
#include "WorkspacePaneId.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    // Mirrors AppConfigStore.cpp's own (unexported) toString(WorkspacePaneId) -- not worth sharing
    // across those two unrelated concerns (config serialization vs. UI Automation id).
    QString paneAutomationSuffix(WorkspacePaneId id)
    {
        switch (id)
        {
            case WorkspacePaneId::PaneA:
                return QStringLiteral("PaneA");
            case WorkspacePaneId::PaneB:
                return QStringLiteral("PaneB");
            case WorkspacePaneId::PaneC:
                return QStringLiteral("PaneC");
            case WorkspacePaneId::PaneD:
                return QStringLiteral("PaneD");
        }
        return QString();
    }

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

    // Width always left over after the tab bar for WorkspacePaneWidget::m_newTabButton to occupy
    // -- see updateNewTabButtonGeometry(). Comparable to a single scroll-arrow-pair's width so the
    // reserved strip doesn't look oddly large once scroll buttons are actually showing there too.
    constexpr int kNewTabButtonReservedWidth = 32;

    // Hover preview popup (Architecture.md §14.76): decode at the size it's actually displayed at
    // (MediaHoverPreviewPopup's own max display size), no upscaling.
    constexpr int kHoverPreviewMaxSize = 360;

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
                                          ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel,
                                          DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                                          NotificationCenter& notificationCenter, UiThemeChangeNotifier& uiThemeChangeNotifier,
                                          const std::array<int, FileListModel::ColumnCount>& initialColumnWidths,
                                          const std::array<int, FileListModel::ColumnCount>& initialColumnOrder,
                                          QWidget* parent)
    : QWidget(parent)
    , m_pane(pane)
    , m_fileOperationsController(fileOperationsController)
    , m_columnVisibilitySettingsViewModel(columnVisibilitySettingsViewModel)
    , m_diskUsageService(diskUsageService)
    , m_fragmentationService(fragmentationService)
    , m_notificationCenter(notificationCenter)
    , m_uiThemeChangeNotifier(uiThemeChangeNotifier)
    , m_initialColumnWidths(initialColumnWidths)
    , m_initialColumnOrder(initialColumnOrder)
{
    setObjectName(QStringLiteral("workspacePane_") + paneAutomationSuffix(pane->id()));

    createViewModeActions();
    QToolBar* toolBar = createToolBar();
    createTabArea();
    applyThemeColors();
    connect(&m_uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &WorkspacePaneWidget::applyThemeColors);

    m_statusBar = new StatusBarWidget(this);
    m_statusBar->setObjectName(QStringLiteral("paneStatusBar"));
    connect(m_statusBar, &StatusBarWidget::quickSelectTextChanged, this, &WorkspacePaneWidget::onQuickSelectTextChanged);
    connect(m_statusBar, &StatusBarWidget::quickSelectCancelled, this, [this]() {
        if (auto* browserView = currentBrowserView())
        {
            browserView->focusView();
        }
    });
    connect(m_statusBar, &StatusBarWidget::branchButtonClicked, this, &WorkspacePaneWidget::onBranchButtonClicked);
    connect(m_statusBar, &StatusBarWidget::branchSelected, this, &WorkspacePaneWidget::onBranchSelected);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolBar);
    layout->addWidget(m_tabWidget);
    layout->addWidget(m_statusBar);

    connect(m_pane, &WorkspacePaneViewModel::tabAdded, this, &WorkspacePaneWidget::onTabAdded);
    connect(m_pane, &WorkspacePaneViewModel::tabClosed, this, &WorkspacePaneWidget::onTabClosed);
    connect(m_pane, &WorkspacePaneViewModel::activeTabChanged, this, &WorkspacePaneWidget::onActiveTabChanged);

    {
        // Blocked: QTabWidget::insertTab auto-selects the very first tab it receives, which would
        // otherwise fire currentChanged(0) synchronously and have onTabWidgetCurrentChanged feed
        // that back into m_pane->setActiveTab(0) -- clobbering an already-correct restored active
        // index (e.g. from WorkspaceController::restoreFromConfig, which runs before this widget
        // even exists) back down to 0. The explicit setCurrentIndex()/bindToolBarToTab() call below
        // re-syncs the widget to the pane's real active tab regardless.
        const QSignalBlocker blocker(m_tabWidget);
        for (int i = 0; i < m_pane->tabCount(); ++i)
        {
            addPageForTab(m_pane->tabAt(i), i);
        }
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

    m_backAction = toolBar->addAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("Back"));
    m_forwardAction = toolBar->addAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("Forward"));
    m_upAction = toolBar->addAction(style()->standardIcon(QStyle::SP_FileDialogToParent), tr("Up"));

    m_backAction->setEnabled(false);
    m_forwardAction->setEnabled(false);
    m_upAction->setEnabled(false);

    if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(m_backAction)))
    {
        button->setObjectName(QStringLiteral("paneBackButton"));
    }
    if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(m_forwardAction)))
    {
        button->setObjectName(QStringLiteral("paneForwardButton"));
    }
    if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(m_upAction)))
    {
        button->setObjectName(QStringLiteral("paneUpButton"));
    }

    auto* viewModeMenu = new QMenu(this);
    viewModeMenu->addActions(m_viewModeActions);

    m_viewModeButton = new QToolButton(toolBar);
    m_viewModeButton->setObjectName(QStringLiteral("paneViewModeButton"));
    m_viewModeButton->setPopupMode(QToolButton::InstantPopup);
    m_viewModeButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_viewModeButton->setToolTip(tr("View"));
    m_viewModeButton->setMenu(viewModeMenu);
    toolBar->addWidget(m_viewModeButton);

    m_refreshAction = toolBar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh"));
    m_refreshAction->setEnabled(false);
    if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(m_refreshAction)))
    {
        button->setObjectName(QStringLiteral("paneRefreshButton"));
    }

    m_addressBar = new AddressBarWidget(toolBar);
    m_addressBar->setObjectName(QStringLiteral("paneAddressBar"));
    m_addressBar->setClearButtonEnabled(true);
    toolBar->addWidget(m_addressBar);

    m_mediaViewerSortViewBar = new MediaViewerSortViewBar(toolBar);
    m_mediaViewerSortViewBar->setObjectName(QStringLiteral("paneMediaViewerSortViewBar"));
    m_mediaViewerSortViewBarAction = toolBar->addWidget(m_mediaViewerSortViewBar);
    m_mediaViewerSortViewBarAction->setVisible(false);

    connect(m_backAction, &QAction::triggered, this, &WorkspacePaneWidget::onBackRequested);
    connect(m_forwardAction, &QAction::triggered, this, &WorkspacePaneWidget::onForwardRequested);
    connect(m_upAction, &QAction::triggered, this, &WorkspacePaneWidget::onUpRequested);
    connect(m_refreshAction, &QAction::triggered, this, [this]() { if (m_boundTab) m_boundTab->refresh(); });
    connect(m_addressBar, &QLineEdit::returnPressed, this, &WorkspacePaneWidget::onAddressBarEdited);
    connect(m_addressBar, &AddressBarWidget::folderSuggestionsRequested, this,
            &WorkspacePaneWidget::onAddressBarFolderSuggestionsRequested);

    connect(m_mediaViewerSortViewBar, &MediaViewerSortViewBar::sortCriterionChanged, this,
            [this](MediaViewerSortCriterion criterion) { if (m_boundTab) m_boundTab->setMediaViewerSortCriterion(criterion); });
    connect(m_mediaViewerSortViewBar, &MediaViewerSortViewBar::sortAscendingChanged, this,
            [this](bool ascending) { if (m_boundTab) m_boundTab->setMediaViewerSortAscending(ascending); });
    connect(m_mediaViewerSortViewBar, &MediaViewerSortViewBar::viewModeChanged, this,
            [this](MediaViewerViewMode mode) { if (m_boundTab) m_boundTab->setMediaViewerViewMode(mode); });
    connect(m_mediaViewerSortViewBar, &MediaViewerSortViewBar::groupModeChanged, this,
            [this](MediaViewerGroupMode mode) { if (m_boundTab) m_boundTab->setMediaViewerGroupMode(mode); });
    connect(m_mediaViewerSortViewBar, &MediaViewerSortViewBar::previewModeChanged, this,
            [this](MediaViewerPreviewMode mode) { if (m_boundTab) m_boundTab->setMediaViewerPreviewMode(mode); });

    return toolBar;
}

void WorkspacePaneWidget::createTabArea()
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("paneTabWidget"));
    m_tabWidget->setTabsClosable(tabCloseButtonsEnabled());
    m_tabWidget->setMovable(false);
    m_tabWidget->tabBar()->installEventFilter(this);
    m_tabWidget->installEventFilter(this);

    // usesScrollButtons(true) (the default on most styles, forced on here since we're relying on
    // it) makes the tab bar respond to running out of room by handing space to left/right scroll
    // arrows instead of squeezing tabs down to nothing -- but only once IT decides it's short on
    // room, which happens against whatever width it's actually given. A corner widget doesn't
    // reliably shrink that given width: QTabWidget's corner-widget/tab-bar negotiation gives the
    // tab bar priority once its scroll buttons are needed, clawing the corner widget's reserved
    // slot back to zero right when it's needed most (the bug this fixes). Capping the tab bar's
    // own maximumWidth in updateNewTabButtonGeometry() instead is a constraint Qt's layout never
    // violates, so the scroll buttons end up drawn inside that reduced width and
    // kNewTabButtonReservedWidth of pane width is always left over for m_newTabButton to occupy.
    m_tabWidget->tabBar()->setUsesScrollButtons(true);

    // Manually positioned (not a corner widget -- see the comment above and the member comment in
    // the header) to cover the entire blank tab-bar strip after the last tab, however wide that
    // happens to be, so pressing anywhere in it -- not just some fixed-size button -- adds a tab.
    // Styled to be visually inert (no hover/pressed/focus chrome) so it still reads as ordinary
    // tab-bar space.
    m_newTabButton = new QToolButton(m_tabWidget);
    m_newTabButton->setObjectName(QStringLiteral("paneNewTabButton"));
    m_newTabButton->setFocusPolicy(Qt::NoFocus);
    m_newTabButton->setStyleSheet(QStringLiteral(
        "QToolButton#paneNewTabButton { border: none; background: transparent; }"
        "QToolButton#paneNewTabButton:hover { border: none; background: transparent; }"
        "QToolButton#paneNewTabButton:pressed { border: none; background: transparent; }"));
    m_newTabButton->setToolTip(tr("New tab"));
    connect(m_newTabButton, &QToolButton::clicked, this, &WorkspacePaneWidget::onNewTabRequested);
    updateNewTabButtonGeometry();
    m_newTabButton->raise();

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &WorkspacePaneWidget::onTabWidgetCurrentChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &WorkspacePaneWidget::onTabCloseRequested);
}

void WorkspacePaneWidget::applyThemeColors()
{
    const UiColors::UiColorSet& colors = UiColors::current();

    m_toolBar->setStyleSheet(QStringLiteral("QToolBar#paneToolBar { background: %1; } "
                                             "QToolBar#paneToolBar[paneActive=\"true\"] { background: %2; }")
                                  .arg(QLatin1String(colors.navigationPanelBackground), QLatin1String(colors.activePaneAccentBackground)));

    m_tabWidget->setStyleSheet(QStringLiteral(
        // The selected-tab rule below adds a 2px bottom border; giving every tab (not just the
        // selected one) that same border width -- transparent when unselected -- reserves the
        // space up front so switching tabs doesn't change tab height/width and jitter the tab bar.
        "QTabWidget#paneTabWidget::pane { border-top: 1px solid palette(mid); }"
        "QTabWidget#paneTabWidget QTabBar::tab { border-bottom: 2px solid transparent; min-width: 80px; max-width: 220px; }"
        "QTabWidget#paneTabWidget QTabBar::tab:selected { background: %1; border-bottom: 2px solid %2; }")
        .arg(QLatin1String(colors.activeTabBackground), QLatin1String(colors.accentBlue)));
}

void WorkspacePaneWidget::updateNewTabButtonGeometry()
{
    QTabBar* tabBar = m_tabWidget->tabBar();

    // See the comment above setUsesScrollButtons() in createTabArea(): this hard cap is what
    // actually guarantees the reserved strip, rather than a corner widget.
    const int maxTabBarWidth = std::max(0, m_tabWidget->width() - kNewTabButtonReservedWidth);
    if (tabBar->maximumWidth() != maxTabBarWidth)
    {
        tabBar->setMaximumWidth(maxTabBarWidth);
    }

    const int height = tabBar->height() > 0 ? tabBar->height() : tabBar->sizeHint().height();
    const int left = tabBar->geometry().right() + 1;
    const int width = std::max(0, m_tabWidget->width() - left);
    m_newTabButton->setGeometry(left, 0, width, height);
}

void WorkspacePaneWidget::addPageForTab(TabViewModel* tab, int index)
{
    auto* stack = new QStackedWidget(m_tabWidget);

    auto* browserView = new FileBrowserView(tab->fileListModel(), stack);
    browserView->setViewMode(tab->viewMode());
    browserView->setColumnWidths(m_initialColumnWidths);
    browserView->setColumnOrder(m_initialColumnOrder);
    applyDefaultColumnVisibility(browserView);
    applyRestoredColumnVisibility(browserView, tab);
    wireBrowserView(browserView, tab);
    stack->addWidget(browserView); // page 0: normal browsing

    auto* advancedSearchPane = new SearchResultsPane(tab->advancedSearchResultsModel(), m_uiThemeChangeNotifier, stack);
    advancedSearchPane->setCriteria(tab->advancedSearchCriteria(), tab->resolveTagCriteria());
    advancedSearchPane->browserView()->setColumnWidths(m_initialColumnWidths);
    advancedSearchPane->browserView()->setColumnOrder(m_initialColumnOrder);
    applyDefaultColumnVisibility(advancedSearchPane->browserView());
    applyRestoredColumnVisibility(advancedSearchPane->browserView(), tab);
    wireBrowserView(advancedSearchPane->browserView(), tab);
    stack->addWidget(advancedSearchPane); // page 1: advanced search results

    // Media Viewer mode (Architecture.md §14.61): a third FileBrowserView, locked to the icon-grid
    // rendering, bound to this tab's own media-viewer results model. Selection/activation/context
    // menu all come for free via the same wireBrowserView() every other page already goes through.
    auto* mediaViewerBrowserView = new FileBrowserView(tab->mediaViewerResultsModel(), stack, FileBrowserView::DisplayMode::MediaViewerGrid);
    wireBrowserView(mediaViewerBrowserView, tab);
    stack->addWidget(mediaViewerBrowserView); // page 2: Media Viewer grid

    // Keeps the grid itself in sync with the tab's view-mode state (Architecture.md §14.69) --
    // previously mediaViewerViewModeChanged only reached the toolbar combo
    // (MediaViewerSortViewBar::setViewMode, resync only), never the grid, which is why the combo
    // had no visible effect before this.
    connect(tab, &TabViewModel::mediaViewerViewModeChanged, mediaViewerBrowserView, &FileBrowserView::setMediaViewerViewMode);
    mediaViewerBrowserView->setMediaViewerViewMode(tab->mediaViewerViewMode());

    // Keeps the grid's per-tile GIF animation in sync with the tab's preview-mode state
    // (Architecture.md §14.83), same reasoning as the view-mode wiring just above.
    connect(tab, &TabViewModel::mediaViewerPreviewModeChanged, mediaViewerBrowserView, &FileBrowserView::setMediaViewerPreviewMode);
    mediaViewerBrowserView->setMediaViewerPreviewMode(tab->mediaViewerPreviewMode());

    // Keeps this tab's two FileBrowserView instances in sync (Architecture.md §14.x): toggling a
    // column in one (normally the visible one, but either works) hides/shows it in the other too,
    // since visibility is a per-*tab* concept, not a per-view one. The Search tab's own results
    // (Architecture.md §14.42) render in the left panel's SearchPanelWidget instead, which has its
    // own independent FileListModel/visibility and isn't part of this mirroring.
    connect(browserView, &FileBrowserView::columnVisibilityChanged, advancedSearchPane->browserView(), &FileBrowserView::setColumnVisible);
    connect(advancedSearchPane->browserView(), &FileBrowserView::columnVisibilityChanged, browserView, &FileBrowserView::setColumnVisible);

    // Records this tab's own explicit header-menu choice (Architecture.md §14.49) the moment it's
    // toggled from either mirrored view, so captureConfig() always reads back the live state with
    // no need to re-query FileBrowserView at close/save time.
    connect(browserView, &FileBrowserView::columnVisibilityChanged, tab,
            [tab](FileListModel::Column column, bool visible) { tab->setColumnHidden(column, !visible); });
    connect(advancedSearchPane->browserView(), &FileBrowserView::columnVisibilityChanged, tab,
            [tab](FileListModel::Column column, bool visible) { tab->setColumnHidden(column, !visible); });

    connect(tab, &TabViewModel::currentPathChanged, this, [this, tab](const std::filesystem::path& path) {
        const int idx = indexOfTab(tab);
        if (idx >= 0)
        {
            m_tabWidget->setTabText(idx, tabLabelFor(path));
        }
    });

    // The Search tab's own quick search (Architecture.md §14.42) no longer has an in-place page
    // here -- its results render in the left panel's SearchPanelWidget instead, so this pane's
    // stack only ever switches between normal browsing, Advanced Search's in-place results, and
    // Media Viewer mode's grid (Architecture.md §14.61).
    auto pickStackPage = [stack, tab]() { stack->setCurrentIndex(tab->mediaViewerActive() ? 2 : (tab->advancedSearchActive() ? 1 : 0)); };
    connect(tab, &TabViewModel::advancedSearchModeChanged, stack, [pickStackPage](bool) { pickStackPage(); });
    connect(tab, &TabViewModel::mediaViewerModeChanged, stack, [pickStackPage](bool) { pickStackPage(); });

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
                const SearchCriteria& criteria = tab->advancedSearchCriteria();
                advancedSearchPane->setHighlightQuery(QString::fromStdString(criteria.nameQuery), criteria.nameOptions.caseSensitive);
            });

    connect(tab, &TabViewModel::advancedSearchCriteriaChanged, advancedSearchPane,
            [advancedSearchPane, tab](const SearchCriteria& criteria) {
                advancedSearchPane->setCriteria(criteria, tab->resolveTagCriteria());
            });

    // Busy state around AdvancedSearchService's async recursive-scan/tag-first-candidate requests
    // (Architecture.md §14.37). Deliberately NOT tied to advancedSearchResultsChanged above --
    // that also fires for a long scan's intermediate progress ticks, and the busy indicator should
    // stay up for the whole scan, not clear after the first partial result. advancedSearchLoadingFinished
    // covers the success case, advancedSearchFailed the error case.
    connect(tab, &TabViewModel::advancedSearchLoading, advancedSearchPane,
            [advancedSearchPane]() { advancedSearchPane->criteriaPanel()->setSearching(true); });
    connect(tab, &TabViewModel::advancedSearchLoadingFinished, advancedSearchPane,
            [advancedSearchPane]() { advancedSearchPane->criteriaPanel()->setSearching(false); });
    connect(tab, &TabViewModel::advancedSearchFailed, advancedSearchPane,
            [advancedSearchPane](const QString&) { advancedSearchPane->criteriaPanel()->setSearching(false); });

    // Bottom panel (Architecture.md §14.24): a splitter over the existing browse/search/
    // advanced-search stack and a collapsible BottomPanelWidget with a "Terminal" tab, wrapped
    // as the actual tab page instead of inserting `stack` directly. The Terminal tab's content is
    // TerminalSessionsWidget, which can hold several concurrent cmd/PowerShell sessions rather than
    // the single always-cmd TerminalWidget this used to be.
    auto* bottomPanel = new BottomPanelWidget(m_uiThemeChangeNotifier, m_tabWidget);

    // Media Viewer mode hides the bottom panel for the duration (Architecture.md §14.61) -- a
    // per-tab widget, so wiring it directly to this tab's own signal (regardless of whether this
    // tab is currently the bound/focused one) is safe; it only ever affects this tab's own page.
    connect(tab, &TabViewModel::mediaViewerModeChanged, bottomPanel, [bottomPanel](bool active) { bottomPanel->setVisible(!active); });

    auto* terminalSessions = new TerminalSessionsWidget(m_uiThemeChangeNotifier, bottomPanel);
    terminalSessions->setObjectName(QStringLiteral("terminalSessionsWidget"));
    const int terminalPanelTabIndex = bottomPanel->addPanelTab(tr("Terminal"), terminalSessions);

    // Second bottom-panel tab, right after Terminal (Architecture.md §14.39).
    auto* folderSizeWidget = new FolderSizeWidget(m_diskUsageService, m_notificationCenter, tab, bottomPanel);
    bottomPanel->addPanelTab(tr("Folder Size"), folderSizeWidget);

    // Third bottom-panel tab, right after Folder Size (Architecture.md §14.52).
    auto* fragmentationWidget = new FragmentationWidget(m_fragmentationService, m_notificationCenter, tab, bottomPanel);
    bottomPanel->addPanelTab(tr("Fragmentation"), fragmentationWidget);
    connect(fragmentationWidget, &FragmentationWidget::openContainingFolderRequested, this,
            [this](const std::filesystem::path& path) { navigateAndSelect(path.parent_path(), path); });

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

    auto resolveTerminalCwd = [tab]() {
        auto cwd = tab->currentPath();
        if (cwd == VirtualPaths::ThisPC || cwd.empty() || ArchiveExtensions::archiveAncestorInPath(cwd))
        {
            cwd = std::filesystem::path(QDir::homePath().toStdWString());
        }
        return cwd;
    };

    connect(terminalSessions, &TerminalSessionsWidget::sessionCreated, terminalSessions,
            [resolveTerminalCwd](TerminalWidget* widget) {
                widget->start(resolveTerminalCwd());
                connect(widget, &TerminalWidget::restartRequested, widget,
                        [widget, resolveTerminalCwd]() { widget->start(resolveTerminalCwd()); });
            });

    connect(bottomPanel, &BottomPanelWidget::panelTabFirstActivated, terminalSessions,
            [terminalSessions, terminalPanelTabIndex](int index) {
                if (index == terminalPanelTabIndex)
                {
                    terminalSessions->activateDefaultSession();
                }
            });

    m_tabWidget->insertTab(index, splitter, tabLabelFor(tab->currentPath()));
}

void WorkspacePaneWidget::applyDefaultColumnVisibility(FileBrowserView* browserView)
{
    for (int column = FileListModel::NameColumn + 1; column < FileListModel::ColumnCount; ++column)
    {
        const auto typedColumn = static_cast<FileListModel::Column>(column);
        browserView->setColumnVisible(typedColumn, m_columnVisibilitySettingsViewModel.defaultVisibility(typedColumn));
    }
}

void WorkspacePaneWidget::applyRestoredColumnVisibility(FileBrowserView* browserView, TabViewModel* tab)
{
    const auto& hiddenColumns = tab->hiddenColumns();
    for (int column = FileListModel::NameColumn + 1; column < FileListModel::ColumnCount; ++column)
    {
        const std::optional<bool>& hidden = hiddenColumns[static_cast<std::size_t>(column)];
        if (!hidden.has_value())
        {
            continue;
        }

        browserView->setColumnVisible(static_cast<FileListModel::Column>(column), !*hidden);
    }
}

void WorkspacePaneWidget::wireBrowserView(FileBrowserView* browserView, TabViewModel* tab)
{
    connect(browserView, &FileBrowserView::itemActivated, tab, [this, tab](const std::filesystem::path& path, bool isDirectory) {
        // Folder-Grouped Browsing's own subfolder tiles (Architecture.md §14.74) -- the only place
        // a Directory-typed FileNode can reach itemActivated from the Media Viewer grid, since
        // normal browsing's own directory rows still go through the unchanged navigateTo() branch
        // below.
        if (tab->mediaViewerActive() && tab->mediaViewerGroupMode() == MediaViewerGroupMode::ByFolder && isDirectory)
        {
            tab->mediaViewerNavigateTo(path);
            return;
        }

        // Archive browsing (Architecture.md §14.28): a real, not-yet-entered archive file
        // navigates in (like a folder) instead of launching an external app. An archive-looking
        // entry already *inside* another archive degrades gracefully via navigateTo's own
        // NotFound handling (nested archives aren't browsable) rather than being special-cased
        // here.
        if (isDirectory || ArchiveExtensions::isArchiveExtension(path))
        {
            if (!confirmLeavingMonitoredScope(tab, path))
            {
                return;
            }
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

    // Media Viewer Details view's per-item tag-pill click (Architecture.md §14.72) -- only ever
    // fires for the Media-Viewer-grid instance in practice, same as the comment on
    // FileBrowserView's own connection to MediaDetailsDelegate::tagActivated.
    connect(browserView, &FileBrowserView::mediaViewerDetailsTagClicked, tab, &TabViewModel::handleMediaViewerDetailsTagClick);

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
    connect(browserView, &FileBrowserView::navigateUpRequested, this, &WorkspacePaneWidget::onUpRequested);
    connect(browserView, &FileBrowserView::navigateBackRequested, this, &WorkspacePaneWidget::onBackRequested);
    connect(browserView, &FileBrowserView::navigateForwardRequested, this, &WorkspacePaneWidget::onForwardRequested);

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

    // Hover preview popup (Architecture.md §14.76): never fires for the Media-Viewer-grid
    // instance (its eventFilter branch is gated on DisplayMode != MediaViewerGrid), so this
    // connection is a safe no-op there, consistent with this function's existing pattern.
    connect(browserView, &FileBrowserView::mediaHoverPreviewRequested, browserView, [browserView, tab](const FileNode& entry) {
        tab->requestHoverPreviewThumbnail(entry, kHoverPreviewMaxSize, kHoverPreviewMaxSize,
                                           [browserView, path = entry.path()](const QImage& image) {
                                               browserView->showHoverPreviewResult(path, image);
                                           });
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
        disconnect(m_boundTab, &TabViewModel::mediaViewerModeChanged, this, &WorkspacePaneWidget::updateToolBarEnabledForMediaViewer);
        disconnect(m_boundTab, &TabViewModel::mediaViewerSortCriterionChanged, m_mediaViewerSortViewBar,
                   &MediaViewerSortViewBar::setSortCriterion);
        disconnect(m_boundTab, &TabViewModel::mediaViewerSortAscendingChanged, m_mediaViewerSortViewBar,
                   &MediaViewerSortViewBar::setSortAscending);
        disconnect(m_boundTab, &TabViewModel::mediaViewerViewModeChanged, m_mediaViewerSortViewBar,
                   &MediaViewerSortViewBar::setViewMode);
        disconnect(m_boundTab, &TabViewModel::mediaViewerGroupModeChanged, m_mediaViewerSortViewBar,
                   &MediaViewerSortViewBar::setGroupMode);
        disconnect(m_boundTab, &TabViewModel::mediaViewerPreviewModeChanged, m_mediaViewerSortViewBar,
                   &MediaViewerSortViewBar::setPreviewMode);
        disconnect(m_boundTab, &TabViewModel::mediaViewerGroupModeChanged, this,
                   &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
        disconnect(m_boundTab, &TabViewModel::mediaViewerViewModeChanged, this,
                   &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
        disconnect(m_boundTab, &TabViewModel::mediaViewerBackAvailableChanged, this,
                   &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
        disconnect(m_boundTab, &TabViewModel::mediaViewerForwardAvailableChanged, this,
                   &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
        disconnect(m_boundTab, &TabViewModel::mediaViewerUpAvailableChanged, this,
                   &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
        disconnect(m_boundTab, &TabViewModel::mediaViewerCurrentFolderChanged, this, &WorkspacePaneWidget::onCurrentPathChanged);

        disconnect(m_boundTab, &TabViewModel::directoryContentsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::advancedSearchResultsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::advancedSearchModeChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
        disconnect(m_boundTab, &TabViewModel::selectedEntriesChanged, this, &WorkspacePaneWidget::refreshStatusCounts);

        disconnect(m_boundTab, &TabViewModel::currentPathChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);
        disconnect(m_boundTab, &TabViewModel::advancedSearchModeChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);

        disconnect(m_boundTab, &TabViewModel::gitRepositorySummaryChanged, this, &WorkspacePaneWidget::updateGitStatusBar);
        disconnect(m_boundTab, &TabViewModel::gitOperationFailed, this, &WorkspacePaneWidget::gitOperationFailed);
    }

    m_boundTab = tab;

    if (!m_boundTab)
    {
        m_backAction->setEnabled(false);
        m_forwardAction->setEnabled(false);
        m_upAction->setEnabled(false);
        m_refreshAction->setEnabled(false);
        m_addressBar->clear();
        m_statusBar->setCounts(0, 0);
        m_statusBar->clearQuickSelect();
        m_statusBar->setGitInfo(false, QString(), 0);
        return;
    }

    connect(m_boundTab, &TabViewModel::currentPathChanged, this, &WorkspacePaneWidget::onCurrentPathChanged);
    connect(m_boundTab, &TabViewModel::backAvailableChanged, m_backAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::forwardAvailableChanged, m_forwardAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::upAvailableChanged, m_upAction, &QAction::setEnabled);
    connect(m_boundTab, &TabViewModel::navigationFailed, this, &WorkspacePaneWidget::onNavigationFailed);
    connect(m_boundTab, &TabViewModel::viewModeChanged, this, &WorkspacePaneWidget::onViewModeChanged);
    connect(m_boundTab, &TabViewModel::mediaViewerModeChanged, this, &WorkspacePaneWidget::updateToolBarEnabledForMediaViewer);
    connect(m_boundTab, &TabViewModel::mediaViewerSortCriterionChanged, m_mediaViewerSortViewBar,
            &MediaViewerSortViewBar::setSortCriterion);
    connect(m_boundTab, &TabViewModel::mediaViewerSortAscendingChanged, m_mediaViewerSortViewBar,
            &MediaViewerSortViewBar::setSortAscending);
    connect(m_boundTab, &TabViewModel::mediaViewerViewModeChanged, m_mediaViewerSortViewBar,
            &MediaViewerSortViewBar::setViewMode);
    connect(m_boundTab, &TabViewModel::mediaViewerGroupModeChanged, m_mediaViewerSortViewBar,
            &MediaViewerSortViewBar::setGroupMode);
    connect(m_boundTab, &TabViewModel::mediaViewerPreviewModeChanged, m_mediaViewerSortViewBar,
            &MediaViewerSortViewBar::setPreviewMode);
    // Folder-Grouped Browsing (Architecture.md §14.74): back/forward/up/address-bar enabled state
    // depends on mediaViewerGroupMode()/mediaViewerViewMode() (whether folder-nav is even active)
    // and on the folder-nav history's own availability, so every one of these needs to re-run
    // updateToolBarEnabledForMediaViewer(), not just mediaViewerModeChanged above.
    connect(m_boundTab, &TabViewModel::mediaViewerGroupModeChanged, this, &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
    connect(m_boundTab, &TabViewModel::mediaViewerViewModeChanged, this, &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
    connect(m_boundTab, &TabViewModel::mediaViewerBackAvailableChanged, this, &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
    connect(m_boundTab, &TabViewModel::mediaViewerForwardAvailableChanged, this, &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
    connect(m_boundTab, &TabViewModel::mediaViewerUpAvailableChanged, this, &WorkspacePaneWidget::onMediaViewerFolderNavChanged);
    // Folder-nav's own address-bar text sync -- mirrors onCurrentPathChanged's role for real
    // navigation (both just set m_addressBar's text from a path), so the same slot serves both.
    connect(m_boundTab, &TabViewModel::mediaViewerCurrentFolderChanged, this, &WorkspacePaneWidget::onCurrentPathChanged);

    connect(m_boundTab, &TabViewModel::directoryContentsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::advancedSearchResultsChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::advancedSearchModeChanged, this, &WorkspacePaneWidget::refreshStatusCounts);
    connect(m_boundTab, &TabViewModel::selectedEntriesChanged, this, &WorkspacePaneWidget::refreshStatusCounts);

    connect(m_boundTab, &TabViewModel::currentPathChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);
    connect(m_boundTab, &TabViewModel::advancedSearchModeChanged, m_statusBar, &StatusBarWidget::clearQuickSelect);

    connect(m_boundTab, &TabViewModel::gitRepositorySummaryChanged, this, &WorkspacePaneWidget::updateGitStatusBar);
    connect(m_boundTab, &TabViewModel::gitOperationFailed, this, &WorkspacePaneWidget::gitOperationFailed);

    // Sync against a tab that already navigated before this binding existed (e.g. tabs restored
    // from the saved workspace config navigate before MainWindow/WorkspacePaneWidget are
    // constructed) -- the *Changed signals above only fire on future navigation, not retroactively.
    m_backAction->setEnabled(m_boundTab->backAvailable());
    m_forwardAction->setEnabled(m_boundTab->forwardAvailable());
    m_upAction->setEnabled(m_boundTab->upAvailable());
    m_refreshAction->setEnabled(true);

    m_addressBar->setText(displayPathText(m_boundTab->currentPath()));
    m_statusBar->clearQuickSelect();
    onViewModeChanged(m_boundTab->viewMode());
    updateToolBarEnabledForMediaViewer(m_boundTab->mediaViewerActive());
    m_mediaViewerSortViewBar->setSortCriterion(m_boundTab->mediaViewerSortCriterion());
    m_mediaViewerSortViewBar->setSortAscending(m_boundTab->mediaViewerSortAscending());
    m_mediaViewerSortViewBar->setViewMode(m_boundTab->mediaViewerViewMode());
    m_mediaViewerSortViewBar->setGroupMode(m_boundTab->mediaViewerGroupMode());
    m_mediaViewerSortViewBar->setPreviewMode(m_boundTab->mediaViewerPreviewMode());
    refreshStatusCounts();
    updateGitStatusBar(m_boundTab->lastGitRepositorySummary());
}

void WorkspacePaneWidget::updateToolBarEnabledForMediaViewer(bool active)
{
    // Media Viewer mode's grid root is fixed to whichever tab entered it, so switching to another
    // tab in this pane while it's active would strand that lock with no visible way back; disable
    // every other tab for the duration instead, re-enabling all of them once it exits.
    const int mediaViewerTabIndex = indexOfTab(m_boundTab);
    for (int i = 0; i < m_tabWidget->count(); ++i)
    {
        m_tabWidget->setTabEnabled(i, !active || i == mediaViewerTabIndex);
    }

    if (active)
    {
        // Folder-Grouped Browsing (Architecture.md §14.74) reverses §14.61's navigation lock, but
        // only for this one combination -- every other combination (Recursive, or ByFolder outside
        // Thumbnails view) keeps back/forward/up/address-bar disabled exactly as before.
        const bool folderNavActive = mediaViewerFolderNavActive();

        m_backAction->setEnabled(folderNavActive && m_boundTab->mediaViewerPeekBack().has_value());
        m_forwardAction->setEnabled(folderNavActive && m_boundTab->mediaViewerPeekForward().has_value());
        m_upAction->setEnabled(folderNavActive && m_boundTab->mediaViewerUpTarget() != m_boundTab->mediaViewerCurrentFolder());
        m_refreshAction->setEnabled(false);
        m_addressBar->setEnabled(folderNavActive);
        if (folderNavActive)
        {
            m_addressBar->setText(displayPathText(m_boundTab->mediaViewerCurrentFolder()));
        }
        m_viewModeButton->setEnabled(false);
        m_mediaViewerSortViewBarAction->setVisible(true);
        m_mediaViewerSortViewBar->setEnabled(true);
        return;
    }

    m_addressBar->setEnabled(true);
    m_viewModeButton->setEnabled(true);
    m_mediaViewerSortViewBarAction->setVisible(false);

    if (!m_boundTab)
    {
        return;
    }

    m_backAction->setEnabled(m_boundTab->backAvailable());
    m_forwardAction->setEnabled(m_boundTab->forwardAvailable());
    m_upAction->setEnabled(m_boundTab->upAvailable());
    m_refreshAction->setEnabled(true);
    // Folder-nav may have repointed the address bar at mediaViewerCurrentFolder() while active --
    // restore it to the tab's real currentPath() now that Media Viewer mode is (or stays) inactive.
    m_addressBar->setText(displayPathText(m_boundTab->currentPath()));
}

bool WorkspacePaneWidget::mediaViewerFolderNavActive() const
{
    return m_boundTab && m_boundTab->mediaViewerActive() && m_boundTab->mediaViewerGroupMode() == MediaViewerGroupMode::ByFolder &&
           m_boundTab->mediaViewerViewMode() == MediaViewerViewMode::Thumbnails;
}

void WorkspacePaneWidget::onMediaViewerFolderNavChanged()
{
    if (!m_boundTab)
    {
        return;
    }

    updateToolBarEnabledForMediaViewer(m_boundTab->mediaViewerActive());
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
    TabViewModel* tab = m_pane->addTab();
    tab->navigateTo(VirtualPaths::ThisPC);
}

void WorkspacePaneWidget::onTabCloseRequested(int index)
{
    TabViewModel* tab = m_pane->tabAt(index);
    if (tab && tab->isMonitoringActive())
    {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(this, tr("Close Tab"), tr("Monitoring will stop if you close this tab. Continue?"));
        if (answer != QMessageBox::Yes)
        {
            return;
        }
    }

    m_pane->closeTab(index);
}

void WorkspacePaneWidget::onUpRequested()
{
    if (!m_boundTab)
    {
        return;
    }

    if (mediaViewerFolderNavActive())
    {
        // Folder-Grouped Browsing (Architecture.md §14.74): confined to m_mediaViewerRoot's
        // subtree, so there's no monitored-scope guard to run here.
        m_boundTab->mediaViewerGoUp();
        return;
    }

    const std::filesystem::path target = m_boundTab->upTarget();
    if (target == m_boundTab->currentPath())
    {
        return;
    }

    if (!confirmLeavingMonitoredScope(m_boundTab, target))
    {
        return;
    }

    m_boundTab->goUp();
}

void WorkspacePaneWidget::onBackRequested()
{
    if (!m_boundTab)
    {
        return;
    }

    if (mediaViewerFolderNavActive())
    {
        m_boundTab->mediaViewerGoBack();
        return;
    }

    const auto target = m_boundTab->peekBack();
    if (!target)
    {
        return;
    }

    if (!confirmLeavingMonitoredScope(m_boundTab, *target))
    {
        return;
    }

    // Same "select the folder we just left" behaviour as Key_Left in FileBrowserView::handleKeyPress
    // -- goBack() runs synchronously (direct-connected through TabViewModel), so by the time it
    // returns m_boundTab->currentPath() already reflects the target if navigation succeeded.
    const std::filesystem::path previousPath = m_boundTab->currentPath();
    m_boundTab->goBack();
    if (m_boundTab->currentPath() != previousPath)
    {
        if (auto* browserView = currentBrowserView())
        {
            browserView->selectEntryByPath(previousPath);
        }
    }
}

void WorkspacePaneWidget::onForwardRequested()
{
    if (!m_boundTab)
    {
        return;
    }

    if (mediaViewerFolderNavActive())
    {
        m_boundTab->mediaViewerGoForward();
        return;
    }

    const auto target = m_boundTab->peekForward();
    if (!target)
    {
        return;
    }

    if (!confirmLeavingMonitoredScope(m_boundTab, *target))
    {
        return;
    }

    m_boundTab->goForward();
}

bool WorkspacePaneWidget::confirmLeavingMonitoredScope(TabViewModel* tab, const std::filesystem::path& target)
{
    if (!tab->isMonitoringActive() || isPathWithinOrEqual(target, tab->monitoringRoot()))
    {
        return true;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Stop Monitoring"),
        tr("Monitoring will stop if you navigate outside %1. Continue?").arg(displayPathText(tab->monitoringRoot())));

    if (answer != QMessageBox::Yes)
    {
        return false;
    }

    tab->stopMonitoring();
    return true;
}

void WorkspacePaneWidget::navigateAndSelect(const std::filesystem::path& directory,
                                             const std::optional<std::filesystem::path>& entryToSelect)
{
    if (!m_boundTab)
    {
        return;
    }

    if (!confirmLeavingMonitoredScope(m_boundTab, directory))
    {
        return;
    }

    m_boundTab->navigateTo(directory);

    if (entryToSelect && m_boundTab->currentPath() == directory)
    {
        if (auto* browserView = currentBrowserView())
        {
            browserView->selectEntryByPath(*entryToSelect);
        }
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
    const std::filesystem::path expanded =
        m_boundTab->expandEnvironmentVariables(std::filesystem::path(trimmed.toStdWString()));
    const QString expandedText = QString::fromStdWString(expanded.wstring());

    if (mediaViewerFolderNavActive())
    {
        // Folder-Grouped Browsing (Architecture.md §14.74): confined to m_mediaViewerRoot's
        // subtree -- "This PC" and the monitored-scope guard below make no sense here.
        std::filesystem::path newPath(expandedText.toStdWString());
        newPath.make_preferred();

        m_boundTab->mediaViewerNavigateTo(newPath);
        if (!pathsEqualCaseInsensitive(m_boundTab->mediaViewerCurrentFolder(), newPath))
        {
            // Outside the confined subtree -- mediaViewerNavigateTo() silently rejected it.
            m_addressBar->setText(displayPathText(m_boundTab->mediaViewerCurrentFolder()));
        }
        return;
    }

    if (expandedText.compare(QStringLiteral("this pc"), Qt::CaseInsensitive) == 0)
    {
        if (!confirmLeavingMonitoredScope(m_boundTab, VirtualPaths::ThisPC))
        {
            m_addressBar->setText(displayPathText(m_boundTab->currentPath()));
            return;
        }
        m_boundTab->navigateTo(VirtualPaths::ThisPC);
        return;
    }

    std::filesystem::path newPath(expandedText.toStdWString());
    newPath.make_preferred();

    if (!confirmLeavingMonitoredScope(m_boundTab, newPath))
    {
        m_addressBar->setText(displayPathText(m_boundTab->currentPath()));
        return;
    }

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
    // Copied rather than referenced: each deletion synchronously emits
    // directoryContentsMayHaveChanged, which refreshes the tab and reassigns its selection,
    // invalidating a reference to selectedEntries() partway through the loop.
    std::vector<std::filesystem::path> paths;
    for (const FileNode& entry : tab->selectedEntries())
    {
        paths.push_back(entry.path());
    }

    FileItemContextMenu::confirmAndDelete(this, *m_fileOperationsController, paths, permanent);
}

void WorkspacePaneWidget::onViewModeChanged(ViewMode mode)
{
    // The tab page is now a QSplitter (browse/advanced-search stack + BottomPanelWidget,
    // Architecture.md §14.23); the stack is always its first direct child (bottomPanel, the
    // splitter's other child, is never itself a QStackedWidget), so this unwrap stays unambiguous.
    auto* splitter = qobject_cast<QSplitter*>(m_tabWidget->currentWidget());
    if (auto* stack = splitter ? qobject_cast<QStackedWidget*>(splitter->widget(0)) : nullptr)
    {
        for (int i = 0; i < stack->count(); ++i)
        {
            // The per-tab stack is heterogeneous: page 0 is a plain FileBrowserView, page 1 is a
            // SearchResultsPane wrapping one. A blind static_cast<FileBrowserView*> on page 1 would
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

std::array<int, FileListModel::ColumnCount> WorkspacePaneWidget::currentColumnOrder() const
{
    if (auto* browserView = currentBrowserView())
    {
        return browserView->columnOrder();
    }

    return m_initialColumnOrder;
}

FileBrowserView* WorkspacePaneWidget::currentBrowserView() const
{
    // Same splitter-unwrap and heterogeneous-stack shape as onViewModeChanged: page 1 is a
    // SearchResultsPane wrapping a FileBrowserView, page 0 is a plain FileBrowserView.
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

    // The Search tab's own quick search (Architecture.md §14.42) renders in the left panel, not in
    // this pane, so it doesn't factor into this pane's own status-bar counts.
    const int total =
        m_boundTab->advancedSearchActive() ? m_boundTab->advancedSearchResultsModel()->rowCount() : m_boundTab->fileListModel()->rowCount();
    const int selected = static_cast<int>(m_boundTab->selectedEntries().size());
    m_statusBar->setCounts(total, selected);
}

void WorkspacePaneWidget::updateGitStatusBar(const GitRepositorySummary& summary)
{
    m_statusBar->setGitInfo(summary.repositoryRoot.has_value(), QString::fromStdString(summary.headDisplayName),
                             summary.changedFileCount);
}

void WorkspacePaneWidget::onBranchButtonClicked()
{
    if (!m_boundTab)
    {
        return;
    }

    m_boundTab->requestLocalBranches([this](Result<std::vector<std::string>> result) {
        if (!m_boundTab)
        {
            // The bound tab changed (or the pane closed) while the fetch was in flight.
            return;
        }

        QStringList branchNames;
        if (result)
        {
            for (const std::string& branchName : result.value())
            {
                branchNames.append(QString::fromStdString(branchName));
            }
        }

        m_statusBar->showBranchMenu(branchNames, QString::fromStdString(m_boundTab->lastGitRepositorySummary().headDisplayName));
    });
}

void WorkspacePaneWidget::onBranchSelected(const QString& branchName)
{
    if (!m_boundTab)
    {
        return;
    }

    m_boundTab->checkoutBranch(branchName);
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
    // The tab bar resizes whenever tabs are added/removed/relabeled or its scroll buttons
    // appear/disappear, and the tab widget resizes whenever the pane itself does -- either can
    // change how much blank strip is left after the last tab, so m_newTabButton's geometry needs
    // recomputing on both.
    if ((watched == m_tabWidget->tabBar() || watched == m_tabWidget) && event->type() == QEvent::Resize)
    {
        updateNewTabButtonGeometry();
    }

    if (watched == m_tabWidget->tabBar() && event->type() == QEvent::MouseButtonRelease)
    {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::MiddleButton)
        {
            const int index = m_tabWidget->tabBar()->tabAt(mouseEvent->pos());
            if (index >= 0)
            {
                onTabCloseRequested(index);
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

    const std::filesystem::path targetPath = paths.front();

    FileItemContextMenu::Callbacks callbacks;
    callbacks.rename = [browserView, targetPath]() { browserView->beginRename(targetPath); };
    callbacks.deleteItems = [this, tab]() { onDeleteRequested(tab, false); };
    callbacks.reportError = [this](const QString& message) { emit navigationFailed(message); };
    callbacks.bulkRename = [this, tab, paths]() { emit bulkRenameRequested(tab, paths); };

    const bool browsingInsideArchive = ArchiveExtensions::archiveAncestorInPath(tab->currentPath()).has_value();
    FileItemContextMenu::show(this, *m_fileOperationsController, paths, browsingInsideArchive, callbacks, globalPos);
}

void WorkspacePaneWidget::showBackgroundContextMenu(TabViewModel* tab, FileBrowserView* browserView, const QPoint& globalPos)
{
    const std::filesystem::path directory = tab->currentPath();
    if (directory == VirtualPaths::ThisPC)
    {
        return;
    }

    const auto ownerWindow = reinterpret_cast<NativeWindowHandle>(window()->winId());
    const bool browsingInsideArchive = ArchiveExtensions::archiveAncestorInPath(directory).has_value();

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

    // Read-only, virtual archive location (Architecture.md §14.28): none of these are meaningful
    // there, same posture as the item-selection context menu.
    pasteAction->setEnabled(!browsingInsideArchive);
    newFolderAction->setEnabled(!browsingInsideArchive);
    propertiesAction->setEnabled(!browsingInsideArchive);

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
