#include "WorkspacePaneViewModel.h"

#include "TabMonitoringViewModel.h"
#include "TabViewModel.h"

WorkspacePaneViewModel::WorkspacePaneViewModel(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                                                const FileDecorationRules& fileDecorationRules,
                                                QObject& fileDecorationsChangeSource, GitStatusService& gitStatusService,
                                                FolderMonitorUseCase& folderMonitorUseCase,
                                                AdvancedSearchService& advancedSearchService,
                                                ContentSearchService& contentSearchService, FileTagsService& fileTagsService,
                                                DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                                                IconLoadingService& iconLoadingService,
                                                MediaThumbnailLoadingService& mediaThumbnailLoadingService,
                                                TagChangeNotifier& tagChangeNotifier, NotificationCenter& notificationCenter,
                                                WorkspacePaneId id, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_fileDecorationRules(fileDecorationRules)
    , m_fileDecorationsChangeSource(fileDecorationsChangeSource)
    , m_gitStatusService(gitStatusService)
    , m_folderMonitorUseCase(folderMonitorUseCase)
    , m_advancedSearchService(advancedSearchService)
    , m_contentSearchService(contentSearchService)
    , m_fileTagsService(fileTagsService)
    , m_diskUsageService(diskUsageService)
    , m_fragmentationService(fragmentationService)
    , m_iconLoadingService(iconLoadingService)
    , m_mediaThumbnailLoadingService(mediaThumbnailLoadingService)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_notificationCenter(notificationCenter)
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
    auto* tab = new TabViewModel(m_fileNavigationUseCase, m_tagManagementUseCase, m_fileDecorationRules,
                                  m_fileDecorationsChangeSource, m_gitStatusService, m_folderMonitorUseCase,
                                  m_advancedSearchService, m_contentSearchService, m_fileTagsService, m_diskUsageService,
                                  m_fragmentationService, m_iconLoadingService, m_mediaThumbnailLoadingService,
                                  m_tagChangeNotifier, m_notificationCenter, this);
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
    if (tab->monitoring()->isActive())
    {
        tab->monitoring()->stop();
    }
    m_tabs.erase(m_tabs.begin() + index);
    tab->deleteLater();

    emit tabClosed(index);

    if (index < m_activeIndex)
    {
        // A tab before the active one shifted everything after it down by one index -- the active
        // TabViewModel itself is unchanged, but its index needs to follow, or Preview/Tags/
        // Monitoring and session-save end up tracking the wrong tab.
        --m_activeIndex;
        emit activeTabChanged(m_activeIndex);
    }
    else if (m_activeIndex >= static_cast<int>(m_tabs.size()))
    {
        m_activeIndex = static_cast<int>(m_tabs.size()) - 1;
        emit activeTabChanged(m_activeIndex);
    }
    else if (m_activeIndex == index)
    {
        emit activeTabChanged(m_activeIndex);
    }
}

void WorkspacePaneViewModel::moveTab(int from, int to)
{
    if (from < 0 || static_cast<size_t>(from) >= m_tabs.size() || to < 0 || static_cast<size_t>(to) >= m_tabs.size())
    {
        return;
    }

    if (from == to)
    {
        return;
    }

    TabViewModel* tab = m_tabs[static_cast<size_t>(from)];
    m_tabs.erase(m_tabs.begin() + from);
    m_tabs.insert(m_tabs.begin() + to, tab);

    // Keep m_activeIndex pointing at the same TabViewModel* it did before the move -- same kind
    // of bookkeeping as closeTab()'s index adjustment, just for a move instead of an erase. No
    // activeTabChanged emission: the active tab's identity hasn't changed, and the tab bar/stack
    // already tracks it correctly on its own (QTabWidget keeps currentWidget() in sync with
    // QTabBar::tabMoved internally), so there's nothing for WorkspacePaneWidget to re-sync.
    if (m_activeIndex == from)
    {
        m_activeIndex = to;
    }
    else if (from < m_activeIndex && m_activeIndex <= to)
    {
        --m_activeIndex;
    }
    else if (to <= m_activeIndex && m_activeIndex < from)
    {
        ++m_activeIndex;
    }

    emit tabMoved(from, to);
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
