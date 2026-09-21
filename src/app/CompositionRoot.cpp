#include "CompositionRoot.h"

#include <filesystem>

#include <QStandardPaths>

#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    // Architecture.md §9: database lives under the app-data location (%LOCALAPPDATA%/Exp-LORer/
    // on Windows), portable to Linux without code changes via QStandardPaths.
    std::filesystem::path databasePath()
    {
        const auto appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return std::filesystem::path(appDataDir.toStdWString()) / "explorer.db";
    }

    // Architecture.md §9/§14.14: session config lives alongside the tagging database, under the
    // same app-data location.
    std::filesystem::path configFilePath()
    {
        const auto appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return std::filesystem::path(appDataDir.toStdWString()) / "config.json";
    }

    // Architecture.md §8/§9/§14.25: thumbnail cache lives under the same app-data location, in its
    // own "cache/thumbnails" subfolder.
    std::filesystem::path thumbnailCacheDir()
    {
        const auto appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return std::filesystem::path(appDataDir.toStdWString()) / "cache" / "thumbnails";
    }

    // Architecture.md §14.26: a persistent user *preference* (edited from Settings, saved
    // immediately), deliberately kept in its own file alongside config.json/explorer.db rather than
    // folded into AppConfig (session/window state, saved only on close).
    std::filesystem::path highlightThemeFilePath()
    {
        const auto appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return std::filesystem::path(appDataDir.toStdWString()) / "highlight_theme.json";
    }

    // Architecture.md §14.29: same persistent-user-preference posture as highlightThemeFilePath()
    // above, in its own file.
    std::filesystem::path fileDecorationsFilePath()
    {
        const auto appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return std::filesystem::path(appDataDir.toStdWString()) / "file_decorations.json";
    }
}

CompositionRoot::CompositionRoot()
    : m_fileNavigationUseCase(m_fileSystemRepository, m_contextMenuProvider)
    , m_tagRepository(databasePath())
    , m_tagManagementUseCase(m_tagRepository, m_fileSystemRepository)
    , m_appConfigStore(configFilePath())
    , m_thumbnailCache(thumbnailCacheDir())
    , m_cachingMediaDecoder(m_mediaDecoder, m_thumbnailCache)
    , m_filePreviewUseCase(m_fileSystemRepository, m_cachingMediaDecoder)
    , m_highlightThemeStore(highlightThemeFilePath())
    , m_highlightTheme(m_highlightThemeStore.load())
    , m_highlightThemeViewModel(m_highlightTheme, m_highlightThemeStore)
    , m_fileDecorationRulesStore(fileDecorationsFilePath())
    , m_fileDecorationRules(m_fileDecorationRulesStore.load())
    , m_fileDecorationsViewModel(m_fileDecorationRules, m_fileDecorationRulesStore)
{
}

CompositionRoot::~CompositionRoot() = default;

std::unique_ptr<TabViewModel> CompositionRoot::createTabViewModel()
{
    return std::make_unique<TabViewModel>(m_fileNavigationUseCase, m_tagManagementUseCase, m_fileDecorationRules,
                                           m_fileDecorationsViewModel);
}

std::unique_ptr<WorkspacePaneViewModel> CompositionRoot::createWorkspacePaneViewModel(WorkspacePaneId id)
{
    return std::make_unique<WorkspacePaneViewModel>(m_fileNavigationUseCase, m_tagManagementUseCase, m_fileDecorationRules,
                                                     m_fileDecorationsViewModel, id);
}

std::unique_ptr<WorkspaceController> CompositionRoot::createWorkspaceController()
{
    return std::make_unique<WorkspaceController>(m_fileNavigationUseCase, m_tagManagementUseCase, m_filePreviewUseCase,
                                                  m_fileSystemRepository, m_fileDecorationRules, m_fileDecorationsViewModel);
}
