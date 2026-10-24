#include "WorkspaceLayoutWidget.h"

#include <algorithm>
#include <filesystem>

#include <QApplication>
#include <QMenu>
#include <QSplitter>
#include <QVBoxLayout>

#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspaceLayoutTopology.h"
#include "WorkspacePaneViewModel.h"
#include "WorkspacePaneWidget.h"

namespace
{
    int indexOf(WorkspacePaneId id)
    {
        return static_cast<int>(id);
    }

    // "Move to Pane" context menu (spec §3, Milestone M2): plain, geometry-agnostic labels
    // matching WorkspacePaneId.h's own naming philosophy -- deliberately not "Top Left"-style,
    // since the same slot plays a different visual role per SplitLayout.
    QString paneDisplayName(WorkspacePaneId id)
    {
        switch (id)
        {
            case WorkspacePaneId::PaneA:
                return QObject::tr("Pane A");
            case WorkspacePaneId::PaneB:
                return QObject::tr("Pane B");
            case WorkspacePaneId::PaneC:
                return QObject::tr("Pane C");
            case WorkspacePaneId::PaneD:
                return QObject::tr("Pane D");
        }
        return QString();
    }
}

std::vector<WorkspacePaneId> WorkspaceLayoutWidget::effectiveVisiblePanes(SplitLayout layout) const
{
    if (m_forcedSinglePane)
    {
        return { *m_forcedSinglePane };
    }
    return WorkspaceLayoutTopology::visiblePanes(layout);
}

WorkspaceLayoutWidget::WorkspaceLayoutWidget(WorkspaceController* controller,
                                              ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel,
                                              DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                                              NotificationCenter& notificationCenter, UiThemeChangeNotifier& uiThemeChangeNotifier,
                                              HotkeyManager& hotkeyManager,
                                              const std::array<int, FileListModel::ColumnCount>& initialColumnWidths,
                                              const std::array<int, FileListModel::ColumnCount>& initialColumnOrder,
                                              QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    m_paneWidgets[indexOf(WorkspacePaneId::PaneA)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneA), m_controller->fileOperationsController(),
                                 columnVisibilitySettingsViewModel, diskUsageService, fragmentationService, notificationCenter,
                                 uiThemeChangeNotifier, hotkeyManager, initialColumnWidths, initialColumnOrder, this);
    m_paneWidgets[indexOf(WorkspacePaneId::PaneB)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneB), m_controller->fileOperationsController(),
                                 columnVisibilitySettingsViewModel, diskUsageService, fragmentationService, notificationCenter,
                                 uiThemeChangeNotifier, hotkeyManager, initialColumnWidths, initialColumnOrder, this);
    m_paneWidgets[indexOf(WorkspacePaneId::PaneC)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneC), m_controller->fileOperationsController(),
                                 columnVisibilitySettingsViewModel, diskUsageService, fragmentationService, notificationCenter,
                                 uiThemeChangeNotifier, hotkeyManager, initialColumnWidths, initialColumnOrder, this);
    m_paneWidgets[indexOf(WorkspacePaneId::PaneD)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneD), m_controller->fileOperationsController(),
                                 columnVisibilitySettingsViewModel, diskUsageService, fragmentationService, notificationCenter,
                                 uiThemeChangeNotifier, hotkeyManager, initialColumnWidths, initialColumnOrder, this);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);

    connect(m_controller, &WorkspaceController::layoutChanged, this, &WorkspaceLayoutWidget::applyLayout);
    connect(m_controller, &WorkspaceController::layoutChanged, this, &WorkspaceLayoutWidget::updatePaneActiveStates);
    connect(m_controller, &WorkspaceController::focusedPaneChanged, this, &WorkspaceLayoutWidget::updatePaneActiveStates);
    connect(qApp, &QApplication::focusChanged, this, &WorkspaceLayoutWidget::onFocusChanged);

    // "Move to Pane" context menu (spec §3, Milestone M2): each pane widget bubbles up a bare
    // tabIndex/globalPos -- this is the only object that knows which pane widget it came from, so
    // the lambda captures that identity rather than threading it through the signal itself.
    for (int i = 0; i < 4; ++i)
    {
        const auto sourcePane = static_cast<WorkspacePaneId>(i);
        connect(m_paneWidgets[static_cast<size_t>(i)], &WorkspacePaneWidget::moveToPaneMenuRequested, this,
                [this, sourcePane](int tabIndex, const QPoint& globalPos) { onMoveToPaneMenuRequested(sourcePane, tabIndex, globalPos); });
    }

    applyLayout(m_controller->layout());
    updatePaneActiveStates();
}

WorkspacePaneWidget* WorkspaceLayoutWidget::paneWidget(WorkspacePaneId id) const
{
    return m_paneWidgets[static_cast<size_t>(indexOf(id))];
}

void WorkspaceLayoutWidget::applyLayout(SplitLayout layout)
{
    const auto visible = effectiveVisiblePanes(layout);
    auto isVisible = [&visible](WorkspacePaneId id) {
        return std::find(visible.begin(), visible.end(), id) != visible.end();
    };

    // Auto-seed newly-revealed empty panes at the focused pane's current path so the user never
    // sees a blank pane.
    for (WorkspacePaneId id : visible)
    {
        WorkspacePaneViewModel* pane = m_controller->pane(id);
        if (pane->tabCount() == 0)
        {
            TabViewModel* focusedTab = m_controller->focusedTab();
            const auto seedPath = focusedTab ? focusedTab->currentPath() : std::filesystem::path();
            TabViewModel* tab = pane->addTab();
            if (!seedPath.empty())
            {
                tab->navigateTo(seedPath);
            }
        }
    }

    // Build the new splitter tree first: QSplitter::addWidget() reparents each visible pane into
    // it, detaching it from the old content tree before that tree is deleted below.
    QWidget* newContent = buildContent(layout);

    for (int i = 0; i < 4; ++i)
    {
        WorkspacePaneWidget* paneWidget = m_paneWidgets[static_cast<size_t>(i)];
        if (isVisible(static_cast<WorkspacePaneId>(i)))
        {
            paneWidget->show();
        }
        else
        {
            paneWidget->setParent(this);
            paneWidget->hide();
        }
    }

    if (m_content)
    {
        m_layout->removeWidget(m_content);

        // In SplitLayout::Single, m_content *is* a WorkspacePaneWidget itself (buildContent()
        // returns the pane directly rather than wrapping it in a QSplitter) — it must never be
        // deleted, only the QSplitter wrapper shells buildContent() otherwise creates fresh on
        // every call.
        const bool isPersistentPane = std::find(m_paneWidgets.begin(), m_paneWidgets.end(), m_content) != m_paneWidgets.end();
        if (!isPersistentPane)
        {
            m_content->deleteLater();
        }
    }

    m_content = newContent;
    m_layout->addWidget(m_content);
}

QWidget* WorkspaceLayoutWidget::buildContent(SplitLayout layout)
{
    if (m_forcedSinglePane)
    {
        WorkspacePaneWidget* pane = m_paneWidgets[indexOf(*m_forcedSinglePane)];
        pane->setParent(this);
        return pane;
    }

    switch (layout)
    {
        case SplitLayout::Single:
        {
            WorkspacePaneWidget* paneA = m_paneWidgets[indexOf(WorkspacePaneId::PaneA)];
            paneA->setParent(this);
            return paneA;
        }

        case SplitLayout::TwoVertical:
        {
            auto* splitter = new QSplitter(Qt::Horizontal);
            splitter->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneA)]);
            splitter->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneB)]);
            return splitter;
        }

        case SplitLayout::TwoHorizontal:
        {
            auto* splitter = new QSplitter(Qt::Vertical);
            splitter->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneA)]);
            splitter->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneB)]);
            return splitter;
        }

        case SplitLayout::FourGrid:
        {
            auto* topRow = new QSplitter(Qt::Horizontal);
            topRow->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneA)]);
            topRow->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneB)]);

            auto* bottomRow = new QSplitter(Qt::Horizontal);
            bottomRow->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneC)]);
            bottomRow->addWidget(m_paneWidgets[indexOf(WorkspacePaneId::PaneD)]);

            auto* outer = new QSplitter(Qt::Vertical);
            outer->addWidget(topRow);
            outer->addWidget(bottomRow);
            return outer;
        }
    }

    WorkspacePaneWidget* paneA = m_paneWidgets[indexOf(WorkspacePaneId::PaneA)];
    paneA->setParent(this);
    return paneA;
}

void WorkspaceLayoutWidget::updatePaneActiveStates()
{
    const auto visible = effectiveVisiblePanes(m_controller->layout());
    const bool multiplePanesVisible = visible.size() > 1;
    const WorkspacePaneId focused = m_controller->focusedPane();

    for (int i = 0; i < 4; ++i)
    {
        const bool active = multiplePanesVisible && static_cast<WorkspacePaneId>(i) == focused;
        m_paneWidgets[static_cast<size_t>(i)]->setActive(active);
    }
}

void WorkspaceLayoutWidget::forceSinglePane(WorkspacePaneId id)
{
    m_forcedSinglePane = id;
    applyLayout(m_controller->layout());
    updatePaneActiveStates();
}

void WorkspaceLayoutWidget::clearForcedSinglePane()
{
    m_forcedSinglePane.reset();
    applyLayout(m_controller->layout());
    updatePaneActiveStates();
}

void WorkspaceLayoutWidget::moveTabBetweenPanes(WorkspacePaneId sourcePane, int sourceIndex, WorkspacePaneId destPane, int destIndex)
{
    WorkspacePaneWidget* sourceWidget = paneWidget(sourcePane);
    QWidget* page = sourceWidget->detachTabPage(sourceIndex); // widget first (still valid pre-move indices)

    TabViewModel* tab = m_controller->moveTab(sourcePane, sourceIndex, destPane, destIndex); // model move + focus
    if (!tab)
    {
        // Shouldn't happen in practice -- both the context menu (onMoveToPaneMenuRequested) and
        // drag-to-move (Milestone M3) pre-check source->tabCount() > 1 before ever calling this --
        // but defensively re-attach the page to where it came from rather than losing it.
        TabViewModel* sourceTabStillAtIndex = m_controller->pane(sourcePane)->tabAt(sourceIndex);
        if (sourceTabStillAtIndex)
        {
            sourceWidget->attachTabPage(sourceTabStillAtIndex, page, sourceIndex);
        }
        return;
    }

    paneWidget(destPane)->attachTabPage(tab, page, destIndex);
}

void WorkspaceLayoutWidget::onMoveToPaneMenuRequested(WorkspacePaneId sourcePane, int tabIndex, const QPoint& globalPos)
{
    WorkspacePaneViewModel* source = m_controller->pane(sourcePane);
    if (source->tabCount() <= 1)
    {
        // Spec: the entry is never reachable when it would empty the pane.
        return;
    }

    std::vector<WorkspacePaneId> others = WorkspaceLayoutTopology::visiblePanes(m_controller->layout());
    others.erase(std::remove(others.begin(), others.end(), sourcePane), others.end());
    if (others.empty())
    {
        // Single layout, or nothing else currently visible -- omit the menu entirely.
        return;
    }

    QMenu menu(this);
    QMenu* moveMenu = menu.addMenu(tr("Move to Pane"));
    for (WorkspacePaneId destPane : others)
    {
        moveMenu->addAction(paneDisplayName(destPane), this, [this, sourcePane, tabIndex, destPane]() {
            moveTabBetweenPanes(sourcePane, tabIndex, destPane, m_controller->pane(destPane)->tabCount());
        });
    }
    menu.exec(globalPos);
}

void WorkspaceLayoutWidget::onFocusChanged(QWidget* old, QWidget* now)
{
    Q_UNUSED(old);

    if (!now)
    {
        return;
    }

    for (int i = 0; i < 4; ++i)
    {
        WorkspacePaneWidget* paneWidget = m_paneWidgets[static_cast<size_t>(i)];
        for (QWidget* w = now; w; w = w->parentWidget())
        {
            if (w == paneWidget)
            {
                m_controller->setFocusedPane(static_cast<WorkspacePaneId>(i));
                return;
            }
        }
    }
}
