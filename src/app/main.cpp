#include "CompositionRoot.h"
#include "MainWindow.h"
#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

#include <QApplication>
#include <QStandardPaths>

#include <filesystem>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    CompositionRoot compositionRoot;
    auto workspaceController = compositionRoot.createWorkspaceController();

    MainWindow mainWindow(workspaceController.get());
    mainWindow.show();

    const auto homePath = std::filesystem::path(
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdWString());

    // MainWindow's construction already built a WorkspaceLayoutWidget, whose applyLayout() has
    // already auto-seeded an empty tab into PaneA (the sole visible pane at startup, under
    // SplitLayout::Single) via its "newly-revealed empty pane" rule. Navigate that existing tab
    // rather than adding a second one.
    workspaceController->pane(WorkspacePaneId::PaneA)->activeTab()->navigateTo(homePath);

    return app.exec();
}
