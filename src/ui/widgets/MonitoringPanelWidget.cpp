#include "MonitoringPanelWidget.h"

#include <deque>
#include <filesystem>
#include <vector>

#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "ArchiveExtensions.h"
#include "FolderChangeEvent.h"
#include "MonitoringLogModel.h"
#include "MonitoringPanelViewModel.h"
#include "TabViewModel.h"
#include "VirtualPaths.h"

MonitoringPanelWidget::MonitoringPanelWidget(MonitoringPanelViewModel* viewModel, QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel)
{
    m_logModel = new MonitoringLogModel(this);

    auto* mainLayout = new QVBoxLayout(this);

    auto* headerLayout = new QHBoxLayout();
    m_startStopButton = new QPushButton(tr("Start Monitoring"), this);
    m_startStopButton->setObjectName(QStringLiteral("monitoringStartStopButton"));
    headerLayout->addWidget(m_startStopButton);
    headerLayout->addStretch(1);

    m_pinButton = new QToolButton(this);
    m_pinButton->setObjectName(QStringLiteral("monitoringPinButton"));
    m_pinButton->setCheckable(true);
    m_pinButton->setText(tr("Pin"));
    m_pinButton->setEnabled(false);
    headerLayout->addWidget(m_pinButton);
    mainLayout->addLayout(headerLayout);

    m_logView = new QListView(this);
    m_logView->setObjectName(QStringLiteral("monitoringLogView"));
    m_logView->setModel(m_logModel);
    mainLayout->addWidget(m_logView, 1);

    m_totalLabel = new QLabel(tr("Total: 0"), this);
    m_totalLabel->setObjectName(QStringLiteral("monitoringTotalLabel"));
    mainLayout->addWidget(m_totalLabel);

    connect(m_startStopButton, &QPushButton::clicked, m_viewModel, &MonitoringPanelViewModel::toggleStartStop);
    connect(m_pinButton, &QToolButton::clicked, m_viewModel, &MonitoringPanelViewModel::togglePin);

    connect(m_viewModel, &MonitoringPanelViewModel::displayedTabChanged, this, &MonitoringPanelWidget::onDisplayedTabChanged);
    connect(m_viewModel, &MonitoringPanelViewModel::monitoringActiveChanged, this, &MonitoringPanelWidget::onMonitoringActiveChanged);
    connect(m_viewModel, &MonitoringPanelViewModel::monitoringPinnedChanged, this, &MonitoringPanelWidget::onMonitoringPinnedChanged);
    connect(m_viewModel, &MonitoringPanelViewModel::monitoringTotalChanged, this, &MonitoringPanelWidget::onMonitoringTotalChanged);
    connect(m_viewModel, &MonitoringPanelViewModel::monitoringLogReset, this, &MonitoringPanelWidget::onMonitoringLogReset);
    connect(m_viewModel, &MonitoringPanelViewModel::monitoringLogAppended, m_logModel, &MonitoringLogModel::appendEntries);
    connect(m_viewModel, &MonitoringPanelViewModel::monitoringLogAppended, this,
            [this](const std::vector<FolderChangeEvent>&) { m_logView->scrollToBottom(); });
    connect(m_viewModel, &MonitoringPanelViewModel::monitoringFailed, this, &MonitoringPanelWidget::monitoringFailed);

    onDisplayedTabChanged(m_viewModel->displayedTab());
    onMonitoringTotalChanged(m_viewModel->displayedTab() ? m_viewModel->displayedTab()->monitoringTotal() : 0);
    onMonitoringLogReset();
}

void MonitoringPanelWidget::onDisplayedTabChanged(TabViewModel* tab)
{
    if (m_trackedTab)
    {
        disconnect(m_trackedTab, &TabViewModel::currentPathChanged, this, &MonitoringPanelWidget::updateStartStopEnabled);
    }

    m_trackedTab = tab;

    if (m_trackedTab)
    {
        connect(m_trackedTab, &TabViewModel::currentPathChanged, this, &MonitoringPanelWidget::updateStartStopEnabled);
    }

    onMonitoringActiveChanged(tab && tab->isMonitoringActive());
    onMonitoringPinnedChanged(tab && tab->isMonitoringPinned());
}

void MonitoringPanelWidget::updateStartStopEnabled()
{
    TabViewModel* tab = m_viewModel->displayedTab();
    if (!tab)
    {
        m_startStopButton->setEnabled(false);
        return;
    }

    const std::filesystem::path path = tab->currentPath();
    const bool validRoot = !path.empty() && path != VirtualPaths::ThisPC && !ArchiveExtensions::archiveAncestorInPath(path);

    // Once active, Stop must stay clickable regardless of where the tab has since navigated to
    // (monitoring keeps watching the original root) -- only Start is gated on a valid root.
    m_startStopButton->setEnabled(tab->isMonitoringActive() || validRoot);
}

void MonitoringPanelWidget::onMonitoringActiveChanged(bool active)
{
    m_startStopButton->setText(active ? tr("Stop Monitoring") : tr("Start Monitoring"));
    m_pinButton->setEnabled(active);
    if (!active)
    {
        m_pinButton->setChecked(false);
    }
    updateStartStopEnabled();
}

void MonitoringPanelWidget::onMonitoringPinnedChanged(bool pinned)
{
    m_pinButton->setChecked(pinned);
}

void MonitoringPanelWidget::onMonitoringTotalChanged(int total)
{
    m_totalLabel->setText(tr("Total: %1").arg(total));
}

void MonitoringPanelWidget::onMonitoringLogReset()
{
    TabViewModel* tab = m_viewModel->displayedTab();
    m_logModel->setEntries(tab ? tab->monitoringLog() : std::deque<FolderChangeEvent>());
}
