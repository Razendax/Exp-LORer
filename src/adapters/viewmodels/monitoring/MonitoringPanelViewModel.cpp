#include "MonitoringPanelViewModel.h"

#include "TabViewModel.h"

MonitoringPanelViewModel::MonitoringPanelViewModel(QObject* parent)
    : QObject(parent)
{
}

void MonitoringPanelViewModel::setFocusedTab(TabViewModel* tab)
{
    if (m_displayedTab && m_displayedTab->isMonitoringPinned())
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

    if (m_displayedTab->isMonitoringActive())
    {
        m_displayedTab->stopMonitoring();
    }
    else
    {
        m_displayedTab->startMonitoring();
        emit monitoringLogReset();
    }
}

void MonitoringPanelViewModel::togglePin()
{
    if (!m_displayedTab)
    {
        return;
    }

    m_displayedTab->setMonitoringPinned(!m_displayedTab->isMonitoringPinned());
}

void MonitoringPanelViewModel::connectDisplayedTab()
{
    if (!m_displayedTab)
    {
        return;
    }

    connect(m_displayedTab, &TabViewModel::monitoringActiveChanged, this, &MonitoringPanelViewModel::monitoringActiveChanged);
    connect(m_displayedTab, &TabViewModel::monitoringLogAppended, this, &MonitoringPanelViewModel::monitoringLogAppended);
    connect(m_displayedTab, &TabViewModel::monitoringTotalChanged, this, &MonitoringPanelViewModel::monitoringTotalChanged);
    connect(m_displayedTab, &TabViewModel::monitoringPinnedChanged, this, &MonitoringPanelViewModel::monitoringPinnedChanged);
    connect(m_displayedTab, &TabViewModel::monitoringFailed, this, &MonitoringPanelViewModel::monitoringFailed);

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

    emit monitoringActiveChanged(m_displayedTab->isMonitoringActive());
    emit monitoringTotalChanged(m_displayedTab->monitoringTotal());
    emit monitoringPinnedChanged(m_displayedTab->isMonitoringPinned());
}
