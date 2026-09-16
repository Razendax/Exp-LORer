#include "CompositionRoot.h"
#include "MainWindow.h"
#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>

#include <filesystem>

int main(int argc, char* argv[])
{
    // QSettings uses INI format for cross-platform consistency rather than the Windows registry
    // (Architecture.md §9); QCoreApplication's organization/application name give the default
    // QSettings constructor somewhere to write (window geometry, bookmarks, the context-menu mode
    // toggle, etc.).
    QCoreApplication::setOrganizationName(QStringLiteral("Exp-LORer"));
    QCoreApplication::setApplicationName(QStringLiteral("Exp-LORer"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QApplication app(argc, argv);

    CompositionRoot compositionRoot;
    const AppConfig initialConfig = compositionRoot.appConfigStore().load();

    auto workspaceController = compositionRoot.createWorkspaceController();
    const bool restored = workspaceController->restoreFromConfig(initialConfig.workspace);

    MainWindow mainWindow(workspaceController.get(), compositionRoot.appConfigStore(), initialConfig);
    mainWindow.show();

    if (!restored)
    {
        const auto homePath = std::filesystem::path(
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdWString());

        // MainWindow's construction already built a WorkspaceLayoutWidget, whose applyLayout() has
        // already auto-seeded an empty tab into PaneA (the sole visible pane at startup, under
        // SplitLayout::Single) via its "newly-revealed empty pane" rule. Navigate that existing tab
        // rather than adding a second one.
        workspaceController->pane(WorkspacePaneId::PaneA)->activeTab()->navigateTo(homePath);
    }

    return app.exec();
}
