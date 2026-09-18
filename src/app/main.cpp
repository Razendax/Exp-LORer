#include "CompositionRoot.h"
#include "ExceptionHandler.h"
#include "ExpLorerApplication.h"
#include "Logging.h"
#include "MainWindow.h"
#include "TabViewModel.h"
#include "VirtualPaths.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

#include <filesystem>

#include <QSettings>
#include <QStandardPaths>

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

    // Architecture.md §9: logs live under the same app-data location as the database/config, in
    // their own "logs" subfolder. Initialized before QApplication so startup failures during Qt
    // construction itself would still be logged (none currently thrown, but keeps ordering simple).
    const auto appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Logging::init(std::filesystem::path(appDataDir.toStdWString()) / "logs");
    Logging::info("Exp-LORer starting");

    ExpLorerApplication app(argc, argv);

    try
    {
        CompositionRoot compositionRoot;
        const AppConfig initialConfig = compositionRoot.appConfigStore().load();

        auto workspaceController = compositionRoot.createWorkspaceController();
        const bool restored = workspaceController->restoreFromConfig(initialConfig.workspace);

        MainWindow mainWindow(workspaceController.get(), compositionRoot.appConfigStore(), compositionRoot.tagManagementUseCase(),
                              initialConfig);
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
