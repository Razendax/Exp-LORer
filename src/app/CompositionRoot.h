#pragma once

#include <memory>

#include "AppConfigStore.h"
#include "CachingMediaDecoder.h"
#include "DirectoryWatchUseCase.h"
#include "FileDecorationRules.h"
#include "FileDecorationRulesStore.h"
#include "FileDecorationsViewModel.h"
#include "FileNavigationUseCase.h"
#include "FilePreviewUseCase.h"
#include "FolderMonitorUseCase.h"
#include "GitStatusService.h"
#include "GitStatusUseCase.h"
#include "HighlightTheme.h"
#include "HighlightThemeStore.h"
#include "HighlightThemeViewModel.h"
#include "LibGit2StatusProvider.h"
#include "MediaDecoder.h"
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
    CompositionRoot();
    ~CompositionRoot();

    std::unique_ptr<TabViewModel> createTabViewModel();
    std::unique_ptr<WorkspacePaneViewModel> createWorkspacePaneViewModel(WorkspacePaneId id);
    std::unique_ptr<WorkspaceController> createWorkspaceController();

    AppConfigStore& appConfigStore() noexcept { return m_appConfigStore; }
    TagManagementUseCase& tagManagementUseCase() noexcept { return m_tagManagementUseCase; }
    SyntaxHighlightEngine& syntaxHighlightEngine() noexcept { return m_syntaxHighlightEngine; }
    HighlightThemeViewModel& highlightThemeViewModel() noexcept { return m_highlightThemeViewModel; }
    FileDecorationsViewModel& fileDecorationsViewModel() noexcept { return m_fileDecorationsViewModel; }

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
    GitStatusService m_gitStatusService;

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
};
