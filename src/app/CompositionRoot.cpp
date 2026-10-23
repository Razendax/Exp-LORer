#include "CompositionRoot.h"

#include "EnvironmentVariableApplier.h"
#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    // Migration (a pre-existing legacy config.json -> workspaces/default.json) must run before the
    // path below is used to construct m_appConfigStore -- listWorkspaces() triggers it as a side
    // effect (WorkspaceStore::ensureMigratedAndNonEmpty), same as every other lazy-migration-on-
    // first-use store in this codebase.
    std::filesystem::path resolveWorkspaceConfigFilePath(WorkspaceStore& workspaceStore, const QString& workspaceId)
    {
        workspaceStore.listWorkspaces();
        return workspaceStore.configFilePath(workspaceId);
    }

    // Architecture.md §14.57: the first time environment_variables.json is empty (a fresh install,
    // or every variable having been deleted), seed it from a snapshot of the process's own current
    // environment -- so the Environment Variables dialog opens showing the variables that already
    // exist (PATH, TEMP, ...) rather than blank -- and persist that snapshot immediately so it
    // becomes the editable baseline from here on, rather than being recomputed on every startup.
    std::vector<EnvironmentVariable> loadOrSeedEnvironmentVariables(EnvironmentVariablesStore& store)
    {
        std::vector<EnvironmentVariable> variables = store.load();
        if (variables.empty())
        {
            variables = EnvironmentVariableApplier::currentProcessEnvironmentSnapshot();
            store.save(variables);
        }
        return variables;
    }
}

// Architecture.md §9: explorer.db, config.json, cache/thumbnails/, highlight_theme.json, and
// file_decorations.json all live directly under appDataDir, resolved once by the caller (main.cpp)
// -- the real app-data location by default, or a --data-dir override for test isolation.
CompositionRoot::CompositionRoot(const std::filesystem::path& appDataDir, const QString& workspaceId)
    : m_fileNavigationUseCase(m_fileSystemRepository, m_contextMenuProvider)
    , m_tagRepository(appDataDir / "explorer.db")
    , m_tagManagementUseCase(m_tagRepository, m_fileSystemRepository)
    , m_workspaceStore(appDataDir)
    , m_appConfigStore(resolveWorkspaceConfigFilePath(m_workspaceStore, workspaceId))
    , m_thumbnailCache(appDataDir / "cache" / "thumbnails")
    , m_cachingMediaDecoder(m_mediaDecoder, m_thumbnailCache)
    , m_filePreviewUseCase(m_fileSystemRepository, m_cachingMediaDecoder)
    , m_mediaProcessingUseCase(m_cachingMediaDecoder)
    , m_textFileEditUseCase(m_fileSystemRepository)
    , m_textFileEditViewModel(m_textFileEditUseCase)
    , m_gitStatusUseCase(m_gitStatusProvider)
    , m_gitBranchUseCase(m_gitBranchProvider)
    , m_gitStatusService(m_gitStatusUseCase, m_gitBranchUseCase)
    , m_advancedSearchService(m_fileNavigationUseCase, m_fileSystemRepository, appDataDir / "explorer.db")
    , m_contentSearchUseCase(m_fileNavigationUseCase, m_fileSystemRepository)
    , m_contentSearchService(m_fileNavigationUseCase, m_fileSystemRepository, m_contentSearchUseCase)
    , m_fileTransferService(m_fileSystemRepository)
    , m_fileTagsService(m_fileSystemRepository, appDataDir / "explorer.db")
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
    , m_environmentVariablesStore(appDataDir / "environment_variables.json")
    , m_environmentVariables(loadOrSeedEnvironmentVariables(m_environmentVariablesStore))
    , m_environmentVariablesViewModel(m_environmentVariables, m_environmentVariablesStore)
    , m_notificationSettingsStore(appDataDir / "notification_settings.json")
    , m_notificationSettings(m_notificationSettingsStore.load())
    , m_notificationSettingsViewModel(m_notificationSettings, m_notificationSettingsStore)
    , m_notificationCenter(m_notificationSettings)
    , m_columnVisibilitySettingsStore(appDataDir / "column_visibility.json")
    , m_columnVisibilitySettings(m_columnVisibilitySettingsStore.load())
    , m_columnVisibilitySettingsViewModel(m_columnVisibilitySettings, m_columnVisibilitySettingsStore)
    , m_diskUsageService(m_diskUsageScanner, appDataDir / "explorer.db")
    , m_duplicateFileScanner(m_fileSystemRepository)
    , m_duplicateFinderUseCase(m_duplicateFileScanner)
    , m_duplicateFinderService(m_duplicateFinderUseCase, m_fileSystemRepository)
    , m_fragmentationScanner(m_fileSystemRepository)
    , m_fragmentationService(m_fragmentationScanner, appDataDir / "explorer.db")
    , m_mediaThumbnailLoadingService(m_mediaProcessingUseCase)
    , m_bulkRenameUseCase(m_fileSystemRepository)
{
}

CompositionRoot::~CompositionRoot() = default;

std::unique_ptr<TabViewModel> CompositionRoot::createTabViewModel()
{
    return std::make_unique<TabViewModel>(m_fileNavigationUseCase, m_tagManagementUseCase, m_fileDecorationRules,
                                           m_fileDecorationsViewModel, m_gitStatusService, m_folderMonitorUseCase,
                                           m_advancedSearchService, m_contentSearchService, m_fileTagsService,
                                           m_diskUsageService, m_fragmentationService, m_iconLoadingService,
                                           m_mediaThumbnailLoadingService, m_tagChangeNotifier, m_notificationCenter);
}

std::unique_ptr<WorkspacePaneViewModel> CompositionRoot::createWorkspacePaneViewModel(WorkspacePaneId id)
{
    return std::make_unique<WorkspacePaneViewModel>(m_fileNavigationUseCase, m_tagManagementUseCase, m_fileDecorationRules,
                                                     m_fileDecorationsViewModel, m_gitStatusService, m_folderMonitorUseCase,
                                                     m_advancedSearchService, m_contentSearchService, m_fileTagsService,
                                                     m_diskUsageService, m_fragmentationService, m_iconLoadingService,
                                                     m_mediaThumbnailLoadingService, m_tagChangeNotifier,
                                                     m_notificationCenter, id);
}

std::unique_ptr<WorkspaceController> CompositionRoot::createWorkspaceController()
{
    return std::make_unique<WorkspaceController>(m_fileNavigationUseCase, m_tagManagementUseCase, m_filePreviewUseCase,
                                                  m_fileSystemRepository, m_fileDecorationRules, m_fileDecorationsViewModel,
                                                  m_gitStatusService, m_directoryWatchUseCase, m_folderMonitorUseCase,
                                                  m_notificationCenter, m_advancedSearchService, m_contentSearchService,
                                                  m_fileTagsService, m_diskUsageService, m_fragmentationService,
                                                  m_iconLoadingService, m_mediaThumbnailLoadingService, m_tagChangeNotifier,
                                                  m_fileTransferService);
}
