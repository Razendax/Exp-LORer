#include "WorkspacePaneViewModel.h"

#include "TabViewModel.h"

WorkspacePaneViewModel::WorkspacePaneViewModel(FileNavigationUseCase& fileNavigationUseCase, WorkspacePaneId id, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_id(id)
{
}

TabViewModel* WorkspacePaneViewModel::tabAt(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= m_tabs.size())
    {
        return nullptr;
    }

    return m_tabs[static_cast<size_t>(index)];
}

TabViewModel* WorkspacePaneViewModel::activeTab() const
{
    return tabAt(m_activeIndex);
}

TabViewModel* WorkspacePaneViewModel::addTab()
{
    auto* tab = new TabViewModel(m_fileNavigationUseCase, this);
    m_tabs.push_back(tab);

    const int index = static_cast<int>(m_tabs.size()) - 1;
    emit tabAdded(index);

    setActiveTab(index);

    return tab;
}

void WorkspacePaneViewModel::closeTab(int index)
{
    if (m_tabs.size() <= 1)
    {
        return;
    }

    if (index < 0 || static_cast<size_t>(index) >= m_tabs.size())
    {
        return;
    }

    TabViewModel* tab = m_tabs[static_cast<size_t>(index)];
    m_tabs.erase(m_tabs.begin() + index);
    tab->deleteLater();

    emit tabClosed(index);

    if (m_activeIndex >= static_cast<int>(m_tabs.size()))
    {
        m_activeIndex = static_cast<int>(m_tabs.size()) - 1;
        emit activeTabChanged(m_activeIndex);
    }
    else if (m_activeIndex == index)
    {
        emit activeTabChanged(m_activeIndex);
    }
}

void WorkspacePaneViewModel::setActiveTab(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_tabs.size())
    {
        return;
    }

    if (index == m_activeIndex)
    {
        return;
    }

    m_activeIndex = index;
    emit activeTabChanged(index);
}
