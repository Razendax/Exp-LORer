#include "CompositionRoot.h"
#include "ExceptionHandler.h"
#include "ExpLorerApplication.h"
#include "Logging.h"
#include "MainWindow.h"
#include "TabViewModel.h"
#include "VirtualPaths.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

#include <cstring>
#include <filesystem>
#include <optional>

#include <QSettings>
#include <QStandardPaths>

#ifdef _WIN32
#include <cwchar>

#include <Windows.h>

#include <Shellapi.h>
#endif

namespace
{
    // Architecture.md §9: looks for a "--data-dir <path>" startup argument, letting ui_tests/ (§11)
    // point the app at an isolated, disposable directory instead of the real app-data location. On
    // Windows, re-reads the process's own command line as wide text rather than trusting the narrow
    // `argv` this function receives -- MSVC's narrow argv is ANSI-code-page-limited, the same
    // narrowing trap PathUtf8/path::string() guidance (Architecture.md §6) warns about, and the
    // override value is itself a filesystem path that could be non-ASCII.
    std::optional<std::filesystem::path> resolveDataDirOverride(int argc, char* argv[])
    {
#ifdef _WIN32
        (void)argc;
        (void)argv;
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (!wargv)
        {
            return std::nullopt;
        }
        std::optional<std::filesystem::path> result;
        for (int i = 1; i < wargc - 1; ++i)
        {
            if (std::wcscmp(wargv[i], L"--data-dir") == 0)
            {
                result = std::filesystem::path(wargv[i + 1]);
                break;
            }
        }
        LocalFree(wargv);
        return result;
#else
        for (int i = 1; i < argc - 1; ++i)
        {
            if (std::strcmp(argv[i], "--data-dir") == 0)
            {
                return std::filesystem::path(argv[i + 1]);
            }
        }
        return std::nullopt;
#endif
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

    ExpLorerApplication app(argc, argv);

    try
    {
        CompositionRoot compositionRoot(appDataDir);
        const AppConfig initialConfig = compositionRoot.appConfigStore().load();

        auto workspaceController = compositionRoot.createWorkspaceController();
        const bool restored = workspaceController->restoreFromConfig(initialConfig.workspace);

        MainWindow mainWindow(workspaceController.get(), compositionRoot.appConfigStore(), compositionRoot.tagManagementUseCase(),
                              compositionRoot.syntaxHighlightEngine(), compositionRoot.highlightThemeViewModel(),
                              compositionRoot.fileDecorationsViewModel(), compositionRoot.favoritesViewModel(),
                              compositionRoot.notificationCenter(), compositionRoot.notificationSettingsViewModel(), initialConfig);
        mainWindow.show();

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
