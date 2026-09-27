#pragma once

#include <filesystem>
#include <memory>

#include "AdvancedSearchService.h"
#include "AppConfigStore.h"
#include "CachingMediaDecoder.h"
#include "DirectoryWatchUseCase.h"
#include "Favorites.h"
#include "FavoritesStore.h"
#include "FavoritesViewModel.h"
#include "FileDecorationRules.h"
#include "FileDecorationRulesStore.h"
#include "FileDecorationsViewModel.h"
#include "FileNavigationUseCase.h"
#include "FilePreviewUseCase.h"
#include "FolderMonitorUseCase.h"
#include "GitBranchUseCase.h"
#include "GitStatusService.h"
#include "GitStatusUseCase.h"
#include "HighlightTheme.h"
#include "HighlightThemeStore.h"
#include "HighlightThemeViewModel.h"
#include "LibGit2BranchProvider.h"
#include "LibGit2StatusProvider.h"
#include "MediaDecoder.h"
#include "NotificationCenter.h"
#include "NotificationSettings.h"
#include "NotificationSettingsStore.h"
#include "NotificationSettingsViewModel.h"
#include "SQLiteTagRepository.h"
#include "ShellContextMenuProvider.h"
#include "StandardDirectoryWatcher.h"
#include "StandardFileSystemRepository.h"
#include "StandardFolderMonitor.h"
#include "SyntaxHighlightEngine.h"
#include "TagManagementUseCase.h"
#include "ThumbnailCache.h"
#include "WorkspacePaneId.h"

class TabViewModel;
class WorkspacePaneViewModel;
class WorkspaceController;

// Wires concrete adapters/repositories into use cases via constructor injection. Owns the shared
// adapter/use-case instances; ViewModels are created only through its factory methods
// (Architecture.md §14.6).
class CompositionRoot
{
public:
    // appDataDir: resolved once in main.cpp (the real app-data location, or a --data-dir override
    // for test isolation, Architecture.md §9) -- explorer.db, config.json, cache/thumbnails/,
    // highlight_theme.json, and file_decorations.json all live under it.
    explicit CompositionRoot(const std::filesystem::path& appDataDir);
    ~CompositionRoot();

    std::unique_ptr<TabViewModel> createTabViewModel();
    std::unique_ptr<WorkspacePaneViewModel> createWorkspacePaneViewModel(WorkspacePaneId id);
    std::unique_ptr<WorkspaceController> createWorkspaceController();

    AppConfigStore& appConfigStore() noexcept { return m_appConfigStore; }
    TagManagementUseCase& tagManagementUseCase() noexcept { return m_tagManagementUseCase; }
    SyntaxHighlightEngine& syntaxHighlightEngine() noexcept { return m_syntaxHighlightEngine; }
    HighlightThemeViewModel& highlightThemeViewModel() noexcept { return m_highlightThemeViewModel; }
    FileDecorationsViewModel& fileDecorationsViewModel() noexcept { return m_fileDecorationsViewModel; }
    FavoritesViewModel& favoritesViewModel() noexcept { return m_favoritesViewModel; }
    NotificationCenter& notificationCenter() noexcept { return m_notificationCenter; }
    NotificationSettingsViewModel& notificationSettingsViewModel() noexcept { return m_notificationSettingsViewModel; }

private:
    StandardFileSystemRepository m_fileSystemRepository;
    ShellContextMenuProvider m_contextMenuProvider;
    FileNavigationUseCase m_fileNavigationUseCase;
    SQLiteTagRepository m_tagRepository;
    TagManagementUseCase m_tagManagementUseCase;
    AppConfigStore m_appConfigStore;

    MediaDecoder m_mediaDecoder;
    ThumbnailCache m_thumbnailCache;
    CachingMediaDecoder m_cachingMediaDecoder;
    FilePreviewUseCase m_filePreviewUseCase;

    // Architecture.md §14.x: per-file/folder git status badges. Provider before use case before
    // service (construction order matches this declaration order).
    LibGit2StatusProvider m_gitStatusProvider;
    GitStatusUseCase m_gitStatusUseCase;

    // Architecture.md §14.34: git branch indicator/checkout, sharing GitStatusService's one
    // background thread with the status badges above rather than getting a thread of its own.
    LibGit2BranchProvider m_gitBranchProvider;
    GitBranchUseCase m_gitBranchUseCase;

    GitStatusService m_gitStatusService;

    // Architecture.md §14.x: async Advanced Search (§14.19/§14.22) recursive-scan and
    // tag-first-candidate requests, off the Qt UI thread. Constructed after m_fileNavigationUseCase
    // and m_fileSystemRepository (needs both); grouped here with the other shared background-work
    // services.
    AdvancedSearchService m_advancedSearchService;

    // Architecture.md §14.x: live directory-update watching for the visible+active tab set.
    // Watcher before use case (construction order matches this declaration order).
    StandardDirectoryWatcher m_directoryWatcher;
    DirectoryWatchUseCase m_directoryWatchUseCase;

    // Architecture.md §14.24: recursive per-tab folder monitoring (Monitoring panel). Monitor
    // before use case (construction order matches this declaration order).
    StandardFolderMonitor m_folderMonitor;
    FolderMonitorUseCase m_folderMonitorUseCase;

    // Architecture.md §14.26: text-preview syntax highlighting -- deliberately no Application-layer
    // Port/use-case for this, just plain adapter instances shared between PreviewPanelWidget's
    // SyntaxHighlighter and SettingsDialog (both need the same engine/theme).
    SyntaxHighlightEngine m_syntaxHighlightEngine;
    HighlightThemeStore m_highlightThemeStore;
    HighlightTheme m_highlightTheme;
    HighlightThemeViewModel m_highlightThemeViewModel;

    // Architecture.md §14.29: per-file/folder font & color customization by name pattern --
    // presentation-only, same posture as the highlight-theme block above.
    FileDecorationRulesStore m_fileDecorationRulesStore;
    FileDecorationRules m_fileDecorationRules;
    FileDecorationsViewModel m_fileDecorationsViewModel;

    // Architecture.md §14.35: user-curated favorites (files/folders, optionally grouped into
    // categories), edited from the Favorites menu / Manage Favorites dialog -- presentation-only,
    // same posture as the file-decoration block above.
    FavoritesStore m_favoritesStore;
    Favorites m_favorites;
    FavoritesViewModel m_favoritesViewModel;

    // Notification system: popup (toast) + progress-bar notifications, presentation-only, same
    // posture as the file-decoration/favorites blocks above. NotificationCenter holds a const&
    // to m_notificationSettings (not the Qt NotificationSettingsViewModel below it), so an edit
    // made via the ViewModel is visible to the dispatcher on its very next call with no signal
    // wiring needed between the two.
    NotificationSettingsStore m_notificationSettingsStore;
    NotificationSettings m_notificationSettings;
    NotificationSettingsViewModel m_notificationSettingsViewModel;
    NotificationCenter m_notificationCenter;
};
