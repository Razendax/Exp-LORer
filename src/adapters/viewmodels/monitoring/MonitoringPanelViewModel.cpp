#include "MonitoringPanelViewModel.h"

#include "TabMonitoringViewModel.h"
#include "TabViewModel.h"

MonitoringPanelViewModel::MonitoringPanelViewModel(QObject* parent)
    : QObject(parent)
{
}

void MonitoringPanelViewModel::setFocusedTab(TabViewModel* tab)
{
    if (m_displayedTab && m_displayedTab->monitoring()->isPinned())
    {
        // Pin locks the panel onto its current tab regardless of focus.
        return;
    }

    if (tab == m_displayedTab)
    {
        return;
    }

    disconnectDisplayedTab();
    m_displayedTab = tab;
    connectDisplayedTab();

    emit displayedTabChanged(tab);
    emit monitoringLogReset();
    emitCurrentState();
}

void MonitoringPanelViewModel::toggleStartStop()
{
    if (!m_displayedTab)
    {
        return;
    }

    if (m_displayedTab->monitoring()->isActive())
    {
        m_displayedTab->monitoring()->stop();
    }
    else
    {
        m_displayedTab->monitoring()->start(m_displayedTab->currentPath());
        emit monitoringLogReset();
    }
}

void MonitoringPanelViewModel::togglePin()
{
    if (!m_displayedTab)
    {
        return;
    }

    m_displayedTab->monitoring()->setPinned(!m_displayedTab->monitoring()->isPinned());
}

void MonitoringPanelViewModel::connectDisplayedTab()
{
    if (!m_displayedTab)
    {
        return;
    }

    TabMonitoringViewModel* monitoring = m_displayedTab->monitoring();
    connect(monitoring, &TabMonitoringViewModel::activeChanged, this, &MonitoringPanelViewModel::monitoringActiveChanged);
    connect(monitoring, &TabMonitoringViewModel::logAppended, this, &MonitoringPanelViewModel::monitoringLogAppended);
    connect(monitoring, &TabMonitoringViewModel::totalChanged, this, &MonitoringPanelViewModel::monitoringTotalChanged);
    connect(monitoring, &TabMonitoringViewModel::pinnedChanged, this, &MonitoringPanelViewModel::monitoringPinnedChanged);
    connect(monitoring, &TabMonitoringViewModel::failed, this, &MonitoringPanelViewModel::monitoringFailed);

    // Self-heals if the displayed tab is closed while pinned/displayed -- WorkspaceController
    // calls setFocusedTab(focusedTab()) again right after any tab-closed bookkeeping anyway, but
    // this guarantees m_displayedTab is never left dangling in the meantime.
    connect(m_displayedTab, &QObject::destroyed, this, [this]() {
        m_displayedTab = nullptr;
        emit displayedTabChanged(nullptr);
        emit monitoringLogReset();
    });
}

void MonitoringPanelViewModel::disconnectDisplayedTab()
{
    if (!m_displayedTab)
    {
        return;
    }

    disconnect(m_displayedTab, nullptr, this, nullptr);
    disconnect(m_displayedTab->monitoring(), nullptr, this, nullptr);
}

void MonitoringPanelViewModel::emitCurrentState()
{
    if (!m_displayedTab)
    {
        emit monitoringActiveChanged(false);
        emit monitoringTotalChanged(0);
        emit monitoringPinnedChanged(false);
        return;
    }

    emit monitoringActiveChanged(m_displayedTab->monitoring()->isActive());
    emit monitoringTotalChanged(m_displayedTab->monitoring()->total());
    emit monitoringPinnedChanged(m_displayedTab->monitoring()->isPinned());
}
