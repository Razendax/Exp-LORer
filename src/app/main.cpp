#include "CompositionRoot.h"
#include "ExceptionHandler.h"
#include "ExpLorerApplication.h"
#include "Logging.h"
#include "MainWindow.h"
#include "TabViewModel.h"
#include "UiTheme.h"
#include "VirtualPaths.h"
#include "WorkspaceController.h"
#include "WorkspaceInstanceGuard.h"
#include "WorkspaceLaunchContext.h"
#include "WorkspacePaneViewModel.h"

#include <cstring>
#include <filesystem>
#include <optional>

#include <QSettings>
#include <QStandardPaths>
#include <QString>

#ifdef _WIN32
#include <cwchar>

#include <Windows.h>

#include <Shellapi.h>
#endif

namespace
{
    // Shared by resolveDataDirOverride and resolveWorkspaceIdOverride below: scans the process's
    // own command line for `flagName` followed by a value, returning that value's raw text. On
    // Windows, re-reads the command line as wide text (`CommandLineToArgvW`) rather than trusting
    // the narrow `argv` main() receives -- MSVC's narrow argv is ANSI-code-page-limited, the same
    // narrowing trap PathUtf8/path::string() guidance (Architecture.md §6) warns about, and an
    // override value can itself be a non-ASCII filesystem path.
#ifdef _WIN32
    std::optional<std::wstring> resolveStartupArgOverride(const wchar_t* flagName)
    {
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (!wargv)
        {
            return std::nullopt;
        }

        std::optional<std::wstring> result;
        for (int i = 1; i < wargc - 1; ++i)
        {
            if (std::wcscmp(wargv[i], flagName) == 0)
            {
                result = wargv[i + 1];
                break;
            }
        }
        LocalFree(wargv);
        return result;
    }
#else
    std::optional<std::string> resolveStartupArgOverride(int argc, char* argv[], const char* flagName)
    {
        for (int i = 1; i < argc - 1; ++i)
        {
            if (std::strcmp(argv[i], flagName) == 0)
            {
                return std::string(argv[i + 1]);
            }
        }
        return std::nullopt;
    }
#endif

    // Architecture.md §9: letting ui_tests/ (§11) point the app at an isolated, disposable
    // directory instead of the real app-data location.
    std::optional<std::filesystem::path> resolveDataDirOverride(int argc, char* argv[])
    {
#ifdef _WIN32
        (void)argc;
        (void)argv;
        if (const std::optional<std::wstring> value = resolveStartupArgOverride(L"--data-dir"))
        {
            return std::filesystem::path(*value);
        }
        return std::nullopt;
#else
        if (const std::optional<std::string> value = resolveStartupArgOverride(argc, argv, "--data-dir"))
        {
            return std::filesystem::path(*value);
        }
        return std::nullopt;
#endif
    }

    // Architecture.md §14.60: selects which saved workspace (workspaces/<id>.json) this process
    // opens -- defaults to "default", the id every install's first/migrated workspace is given.
    QString resolveWorkspaceIdOverride(int argc, char* argv[])
    {
#ifdef _WIN32
        (void)argc;
        (void)argv;
        if (const std::optional<std::wstring> value = resolveStartupArgOverride(L"--workspace"))
        {
            return QString::fromStdWString(*value);
        }
#else
        if (const std::optional<std::string> value = resolveStartupArgOverride(argc, argv, "--workspace"))
        {
            return QString::fromStdString(*value);
        }
#endif
        return QStringLiteral("default");
    }
}

int main(int argc, char* argv[])
{
    // QSettings uses INI format for cross-platform consistency rather than the Windows registry
    // (Architecture.md §9); QCoreApplication's organization/application name give the default
    // QSettings constructor somewhere to write (window geometry, bookmarks, the context-menu mode
    // toggle, etc.). QStandardPaths::writableLocation below relies on these too, and both work
    // without a QCoreApplication instance yet constructed.
    QCoreApplication::setOrganizationName(QStringLiteral("Exp-LORer"));
    QCoreApplication::setApplicationName(QStringLiteral("Exp-LORer"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Architecture.md §9: database, config, cache, and logs all live under this single resolved
    // directory -- the real app-data location by default, or --data-dir when overridden.
    const std::optional<std::filesystem::path> dataDirOverride = resolveDataDirOverride(argc, argv);
    const std::filesystem::path appDataDir = dataDirOverride
        ? *dataDirOverride
        : std::filesystem::path(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdWString());

    // Logs live in their own "logs" subfolder there (§14.22). Initialized before QApplication so
    // startup failures during Qt construction itself would still be logged (none currently thrown,
    // but keeps ordering simple).
    Logging::init(appDataDir / "logs");
    Logging::info("Exp-LORer starting");
    Logging::info(std::string("--data-dir override: ") + (dataDirOverride ? "true" : "false"));

    const QString workspaceId = resolveWorkspaceIdOverride(argc, argv);
    Logging::info("workspace: " + workspaceId.toStdString());

    ExpLorerApplication app(argc, argv);

    // Architecture.md §14.60: a losing process forwards activation to whichever process already
    // owns this exact (appDataDir, workspaceId) and exits here, before CompositionRoot/MainWindow
    // are ever constructed -- needs a QCoreApplication (ExpLorerApplication above) for QLocalSocket.
    WorkspaceInstanceGuard instanceGuard(appDataDir, workspaceId);
    if (!instanceGuard.tryAcquire())
    {
        Logging::info("Another instance already owns this workspace; activated it and exiting");
        Logging::shutdown();
        return 0;
    }

    // Architecture.md §14.50: Fusion is the app's style for both Light and Dark themes (set once,
    // never re-set on a theme toggle); applying the persisted theme's QPalette here, before
    // MainWindow is constructed, means the very first paint is already correct.
    UiTheme::installFusionStyle();

    try
    {
        CompositionRoot compositionRoot(appDataDir, workspaceId);
        UiTheme::setTheme(UiTheme::currentTheme(), compositionRoot.uiThemeChangeNotifier());

        const AppConfig initialConfig = compositionRoot.appConfigStore().load();

        auto workspaceController = compositionRoot.createWorkspaceController();
        const bool restored = workspaceController->restoreFromConfig(initialConfig.workspace);

        MainWindow mainWindow(workspaceController.get(), compositionRoot.appConfigStore(), compositionRoot.tagManagementUseCase(),
                              compositionRoot.tagChangeNotifier(), compositionRoot.advancedSearchService(),
                              compositionRoot.iconLoadingService(), compositionRoot.fileTagsService(),
                              compositionRoot.textFileEditViewModel(),
                              compositionRoot.uiThemeChangeNotifier(),
                              compositionRoot.syntaxHighlightEngine(),
                              compositionRoot.highlightThemeViewModel(), compositionRoot.fileDecorationsViewModel(),
                              compositionRoot.favoritesViewModel(), compositionRoot.notificationCenter(),
                              compositionRoot.notificationSettingsViewModel(), compositionRoot.columnVisibilitySettingsViewModel(),
                              compositionRoot.diskUsageService(), compositionRoot.duplicateFinderService(),
                              compositionRoot.fragmentationService(), compositionRoot.bulkRenameUseCase(),
                              compositionRoot.environmentVariablesViewModel(), compositionRoot.workspaceStore(),
                              WorkspaceLaunchContext{ appDataDir, workspaceId }, initialConfig);
        mainWindow.show();

        QObject::connect(&instanceGuard, &WorkspaceInstanceGuard::activationRequested, &mainWindow, [&mainWindow]() {
            mainWindow.raise();
            mainWindow.activateWindow();
        });

        if (!restored)
        {
            // MainWindow's construction already built a WorkspaceLayoutWidget, whose applyLayout()
            // has already auto-seeded an empty tab into PaneA (the sole visible pane at startup,
            // under SplitLayout::Single) via its "newly-revealed empty pane" rule. Navigate that
            // existing tab rather than adding a second one.
            workspaceController->pane(WorkspacePaneId::PaneA)->activeTab()->navigateTo(VirtualPaths::ThisPC);
        }

        const int exitCode = app.exec();

        Logging::info("Exp-LORer exiting (code " + std::to_string(exitCode) + ")");
        Logging::shutdown();

        return exitCode;
    }
    catch (const std::exception& ex)
    {
        ExceptionHandler::handleFatal(ex.what());
    }
    catch (...)
    {
        ExceptionHandler::handleFatal("Unknown exception");
    }
}
