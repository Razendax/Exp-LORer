#include "CompositionRoot.h"

#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

// Architecture.md §9: explorer.db, config.json, cache/thumbnails/, highlight_theme.json, and
// file_decorations.json all live directly under appDataDir, resolved once by the caller (main.cpp)
// -- the real app-data location by default, or a --data-dir override for test isolation.
CompositionRoot::CompositionRoot(const std::filesystem::path& appDataDir)
    : m_fileNavigationUseCase(m_fileSystemRepository, m_contextMenuProvider)
    , m_tagRepository(appDataDir / "explorer.db")
    , m_tagManagementUseCase(m_tagRepository, m_fileSystemRepository)
    , m_appConfigStore(appDataDir / "config.json")
    , m_thumbnailCache(appDataDir / "cache" / "thumbnails")
    , m_cachingMediaDecoder(m_mediaDecoder, m_thumbnailCache)
    , m_filePreviewUseCase(m_fileSystemRepository, m_cachingMediaDecoder)
    , m_gitStatusUseCase(m_gitStatusProvider)
    , m_gitBranchUseCase(m_gitBranchProvider)
    , m_gitStatusService(m_gitStatusUseCase, m_gitBranchUseCase)
    , m_directoryWatchUseCase(m_directoryWatcher)
    , m_folderMonitorUseCase(m_folderMonitor)
    , m_highlightThemeStore(appDataDir / "highlight_theme.json")
    , m_highlightTheme(m_highlightThemeStore.load())
    , m_highlightThemeViewModel(m_highlightTheme, m_highlightThemeStore)
    , m_fileDecorationRulesStore(appDataDir / "file_decorations.json")
    , m_fileDecorationRules(m_fileDecorationRulesStore.load())
    , m_fileDecorationsViewModel(m_fileDecorationRules, m_fileDecorationRulesStore)
    , m_favoritesStore(appDataDir / "favorites.json")
    , m_favorites(m_favoritesStore.load())
    , m_favoritesViewModel(m_favorites, m_favoritesStore)
    , m_notificationSettingsStore(appDataDir / "notification_settings.json")
    , m_notificationSettings(m_notificationSettingsStore.load())
    , m_notificationSettingsViewModel(m_notificationSettings, m_notificationSettingsStore)
    , m_notificationCenter(m_notificationSettings)
{
}

CompositionRoot::~CompositionRoot() = default;

std::unique_ptr<TabViewModel> CompositionRoot::createTabViewModel()
{
    return std::make_unique<TabViewModel>(m_fileNavigationUseCase, m_tagManagementUseCase, m_fileDecorationRules,
                                           m_fileDecorationsViewModel, m_gitStatusService, m_folderMonitorUseCase);
}

std::unique_ptr<WorkspacePaneViewModel> CompositionRoot::createWorkspacePaneViewModel(WorkspacePaneId id)
{
    return std::make_unique<WorkspacePaneViewModel>(m_fileNavigationUseCase, m_tagManagementUseCase, m_fileDecorationRules,
                                                     m_fileDecorationsViewModel, m_gitStatusService, m_folderMonitorUseCase, id);
}

std::unique_ptr<WorkspaceController> CompositionRoot::createWorkspaceController()
{
    return std::make_unique<WorkspaceController>(m_fileNavigationUseCase, m_tagManagementUseCase, m_filePreviewUseCase,
                                                  m_fileSystemRepository, m_fileDecorationRules, m_fileDecorationsViewModel,
                                                  m_gitStatusService, m_directoryWatchUseCase, m_folderMonitorUseCase,
                                                  m_notificationCenter);
}
