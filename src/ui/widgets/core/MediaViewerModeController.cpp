#include "MediaViewerModeController.h"

#include <QAction>
#include <QActionGroup>
#include <QSignalBlocker>

#include "MediaViewerSearchPanelWidget.h"
#include "SidePanelWidget.h"
#include "TabMediaViewerViewModel.h"
#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspaceLayoutWidget.h"

MediaViewerModeController::MediaViewerModeController(WorkspaceController& workspaceController,
                                                       WorkspaceLayoutWidget& workspaceLayoutWidget,
                                                       SidePanelWidget& leftPanelWidget, SidePanelWidget& rightPanelWidget,
                                                       MediaViewerSearchPanelWidget& searchPanelWidget,
                                                       QActionGroup& layoutActionGroup, QAction* modeAction, QObject* parent)
    : QObject(parent)
    , m_workspaceController(workspaceController)
    , m_workspaceLayoutWidget(workspaceLayoutWidget)
    , m_leftPanelWidget(leftPanelWidget)
    , m_rightPanelWidget(rightPanelWidget)
    , m_searchPanelWidget(searchPanelWidget)
    , m_layoutActionGroup(layoutActionGroup)
    , m_modeAction(modeAction)
{
    connect(m_modeAction, &QAction::toggled, this, [this](bool checked) {
        if (checked)
        {
            // Entering targets the currently-focused tab, same as before.
            if (TabViewModel* tab = m_workspaceController.focusedTab())
            {
                tab->enterMediaViewer();
            }
            return;
        }

        // Exiting targets the tab actually in Media Viewer mode, not focusedTab() -- in a split
        // workspace, focus may have moved to a different tab since Media Viewer mode was entered
        // (Architecture.md §14.61), and exit() on an unrelated tab is a silent no-op.
        if (TabViewModel* tab = targetTab())
        {
            tab->mediaViewer()->exit();
        }
    });

    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::kindChanged, this, [this](MediaViewerKind kind) {
        if (TabViewModel* tab = targetTab())
        {
            tab->mediaViewer()->setKind(kind);
        }
    });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::exitRequested, this, [this]() {
        if (TabViewModel* tab = targetTab())
        {
            tab->mediaViewer()->exit();
        }
    });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::tagSearchChipActivated, this,
            [this](Tag::Id tagId, Qt::KeyboardModifiers modifiers) {
                if (TabViewModel* tab = targetTab())
                {
                    tab->handleMediaViewerSearchTagActivated(tagId, modifiers.testFlag(Qt::ControlModifier));
                }
            });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::tagBehaviourChanged, this,
            [this](MediaViewerTagBehaviour behaviour) {
                if (TabViewModel* tab = targetTab())
                {
                    tab->mediaViewer()->setTagBehaviour(behaviour);
                }
            });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::tagFilterRemoved, this, [this](Tag::Id tagId) {
        if (TabViewModel* tab = targetTab())
        {
            tab->mediaViewer()->removeTagFilter(tagId);
        }
    });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::tagSearchQueryRequested, this,
            [this](const QString& query) {
                if (TabViewModel* tab = targetTab())
                {
                    m_searchPanelWidget.setTagSearchResults(tab->searchTagsForCriteria(query));
                }
            });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::tagSearchQueryAccepted, this,
            [this](const QString& query) {
                if (TabViewModel* tab = targetTab())
                {
                    tab->acceptMediaViewerSearchTagQuery(query);
                }
            });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::tagCreateRequested, this, [this](const QString& name) {
        if (TabViewModel* tab = targetTab())
        {
            tab->createAndApplyMediaViewerTag(name);
        }
    });
    connect(&m_searchPanelWidget, &MediaViewerSearchPanelWidget::nameFilterChanged, this,
            [this](const QString& query, const NameSearchOptions& options) {
                if (TabViewModel* tab = targetTab())
                {
                    tab->mediaViewer()->setNameFilter(query, options);
                }
            });
}

TabViewModel* MediaViewerModeController::targetTab() const
{
    return m_activeTab ? m_activeTab : m_workspaceController.focusedTab();
}

void MediaViewerModeController::onModeActiveChanged(bool active)
{
    m_leftPanelWidget.setVisible(!active);

    if (active)
    {
        m_workspaceLayoutWidget.forceSinglePane(m_workspaceController.focusedPane());
        m_layoutActionGroup.setEnabled(false);

        m_rightPanelActiveIndexBeforeMediaViewer = m_rightPanelWidget.activeTabIndex();
        m_rightPanelWidget.setPanelTabButtonVisible(0, false);
        m_rightPanelWidget.setPanelTabButtonVisible(1, false);
        m_rightPanelWidget.setPanelTabButtonVisible(2, false);
        m_rightPanelWidget.setPanelTabButtonVisible(3, true);
        m_rightPanelWidget.activateTab(3);
        return;
    }

    m_workspaceLayoutWidget.clearForcedSinglePane();
    m_layoutActionGroup.setEnabled(true);

    m_rightPanelWidget.setPanelTabButtonVisible(0, true);
    m_rightPanelWidget.setPanelTabButtonVisible(1, true);
    m_rightPanelWidget.setPanelTabButtonVisible(2, true);
    m_rightPanelWidget.setPanelTabButtonVisible(3, false);

    const int restoreIndex = m_rightPanelActiveIndexBeforeMediaViewer;
    m_rightPanelActiveIndexBeforeMediaViewer = -1;
    if (restoreIndex >= 0)
    {
        m_rightPanelWidget.activateTab(restoreIndex);
    }
    else
    {
        // The panel was collapsed before Media Viewer mode took over -- activateTab(3) just now
        // forced it open, so collapse it back the same way a real click on the active tab would
        // (SidePanelWidget::onPanelTabButtonClicked's "clicking the already-active tab collapses
        // the panel" branch).
        m_rightPanelWidget.activateTab(3);
    }
}

void MediaViewerModeController::setFocusedTab(TabViewModel* tab)
{
    // Tracks whichever tab is actually in Media Viewer mode (m_activeTab), not the newly-focused
    // tab -- in a split workspace, focus can move to an unrelated tab (a different pane, or
    // another tab in the same pane) while a different tab is still showing Media Viewer mode,
    // forced single-pane. Falls back to the newly-focused tab when no tab is currently in Media
    // Viewer mode.
    if (TabViewModel* target = m_activeTab ? m_activeTab : tab; target != m_trackedTab)
    {
        if (m_trackedTab)
        {
            TabMediaViewerViewModel* tracked = m_trackedTab->mediaViewer();
            disconnect(tracked, &TabMediaViewerViewModel::modeChanged, this, nullptr);
            disconnect(tracked, &TabMediaViewerViewModel::failed, this, nullptr);
            // Every resultsChanged/kindChanged/criteriaChanged/loading*/failed/modeChanged
            // connection into m_searchPanelWidget or m_modeAction below is a lambda (never a plain
            // `this` member-function connection) -- disconnecting all of them by receiver alone is
            // safe and precise.
            disconnect(tracked, nullptr, &m_searchPanelWidget, nullptr);
            disconnect(tracked, nullptr, m_modeAction, nullptr);
        }
        m_trackedTab = target;

        if (TabViewModel* viewerTab = m_trackedTab)
        {
            TabMediaViewerViewModel* mediaViewer = viewerTab->mediaViewer();

            {
                const QSignalBlocker modeActionBlocker(m_modeAction);
                m_modeAction->setChecked(mediaViewer->isActive());
            }

            connect(mediaViewer, &TabMediaViewerViewModel::modeChanged, this, [this, viewerTab](bool active) {
                if (active)
                {
                    m_activeTab = viewerTab;
                }
                else if (m_activeTab == viewerTab)
                {
                    m_activeTab = nullptr;
                }
                onModeActiveChanged(active);
            });
            connect(mediaViewer, &TabMediaViewerViewModel::failed, this,
                    [this](const QString& message) { emit statusMessage(message); });

            connect(mediaViewer, &TabMediaViewerViewModel::modeChanged, m_modeAction, [this](bool active) {
                const QSignalBlocker blocker(m_modeAction);
                m_modeAction->setChecked(active);
            });

            connect(mediaViewer, &TabMediaViewerViewModel::kindChanged, &m_searchPanelWidget,
                    [this](MediaViewerKind kind) { m_searchPanelWidget.setKind(kind); });
            connect(mediaViewer, &TabMediaViewerViewModel::criteriaChanged, &m_searchPanelWidget, [this, mediaViewer]() {
                m_searchPanelWidget.setAppliedTagFilters(mediaViewer->resolveTagFilters());
            });
            connect(mediaViewer, &TabMediaViewerViewModel::tagBehaviourChanged, &m_searchPanelWidget,
                    [this](MediaViewerTagBehaviour behaviour) { m_searchPanelWidget.setTagBehaviour(behaviour); });
            connect(mediaViewer, &TabMediaViewerViewModel::loading, &m_searchPanelWidget,
                    [this]() { m_searchPanelWidget.setLoading(true); });
            connect(mediaViewer, &TabMediaViewerViewModel::loadingFinished, &m_searchPanelWidget,
                    [this]() { m_searchPanelWidget.setLoading(false); });
            connect(mediaViewer, &TabMediaViewerViewModel::failed, &m_searchPanelWidget,
                    [this]() { m_searchPanelWidget.setLoading(false); });

            m_searchPanelWidget.setKind(mediaViewer->kind());
            m_searchPanelWidget.setTagBehaviour(mediaViewer->tagBehaviour());
            m_searchPanelWidget.setAppliedTagFilters(mediaViewer->resolveTagFilters());
            m_searchPanelWidget.setNameFilter(mediaViewer->nameFilter(), mediaViewer->nameOptions());
            // Prepopulate with every tag (searchTagsForCriteria("") -> all tags, alphabetically)
            // rather than leaving the matching-tags section empty until the user types something.
            m_searchPanelWidget.setTagSearchResults(viewerTab->searchTagsForCriteria(QString()));
        }
        else
        {
            const QSignalBlocker modeActionBlocker(m_modeAction);
            m_modeAction->setChecked(false);
        }
    }

    if (!tab)
    {
        // Still enabled/checked against m_activeTab's own state when one exists -- it may still
        // be showing Media Viewer mode on another pane even though this pane has no focused tab.
        if (!m_activeTab)
        {
            const QSignalBlocker modeActionBlocker(m_modeAction);
            m_modeAction->setEnabled(false);
        }
        return;
    }

    // The toggle action is always enabled once any tab is focused -- entering Media Viewer mode
    // targets the focused tab, independent of which tab targetTab() currently resolves to above.
    m_modeAction->setEnabled(true);
}
