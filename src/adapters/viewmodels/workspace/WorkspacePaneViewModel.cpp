#include "WorkspacePaneViewModel.h"

#include "TabViewModel.h"

WorkspacePaneViewModel::WorkspacePaneViewModel(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                                                const FileDecorationRules& fileDecorationRules,
                                                QObject& fileDecorationsChangeSource, GitStatusService& gitStatusService,
                                                FolderMonitorUseCase& folderMonitorUseCase,
                                                AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                                                DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                                                IconLoadingService& iconLoadingService,
                                                MediaThumbnailLoadingService& mediaThumbnailLoadingService,
                                                TagChangeNotifier& tagChangeNotifier, WorkspacePaneId id, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_fileDecorationRules(fileDecorationRules)
    , m_fileDecorationsChangeSource(fileDecorationsChangeSource)
    , m_gitStatusService(gitStatusService)
    , m_folderMonitorUseCase(folderMonitorUseCase)
    , m_advancedSearchService(advancedSearchService)
    , m_fileTagsService(fileTagsService)
    , m_diskUsageService(diskUsageService)
    , m_fragmentationService(fragmentationService)
    , m_iconLoadingService(iconLoadingService)
    , m_mediaThumbnailLoadingService(mediaThumbnailLoadingService)
    , m_tagChangeNotifier(tagChangeNotifier)
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
                                  m_advancedSearchService, m_fileTagsService, m_diskUsageService, m_fragmentationService,
                                  m_iconLoadingService, m_mediaThumbnailLoadingService, m_tagChangeNotifier, this);
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
    if (tab->isMonitoringActive())
    {
        tab->stopMonitoring();
    }
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
