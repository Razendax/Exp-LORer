#include "WorkspaceLayoutWidget.h"

#include <algorithm>
#include <filesystem>

#include <QApplication>
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
}

WorkspaceLayoutWidget::WorkspaceLayoutWidget(WorkspaceController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    m_paneWidgets[indexOf(WorkspacePaneId::PaneA)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneA), m_controller->fileOperationsController(), this);
    m_paneWidgets[indexOf(WorkspacePaneId::PaneB)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneB), m_controller->fileOperationsController(), this);
    m_paneWidgets[indexOf(WorkspacePaneId::PaneC)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneC), m_controller->fileOperationsController(), this);
    m_paneWidgets[indexOf(WorkspacePaneId::PaneD)] =
        new WorkspacePaneWidget(m_controller->pane(WorkspacePaneId::PaneD), m_controller->fileOperationsController(), this);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);

    connect(m_controller, &WorkspaceController::layoutChanged, this, &WorkspaceLayoutWidget::applyLayout);
    connect(qApp, &QApplication::focusChanged, this, &WorkspaceLayoutWidget::onFocusChanged);

    applyLayout(m_controller->layout());
}

WorkspacePaneWidget* WorkspaceLayoutWidget::paneWidget(WorkspacePaneId id) const
{
    return m_paneWidgets[static_cast<size_t>(indexOf(id))];
}

void WorkspaceLayoutWidget::applyLayout(SplitLayout layout)
{
    const auto visible = WorkspaceLayoutTopology::visiblePanes(layout);
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
