#include "CompositionRoot.h"
#include "MainWindow.h"
#include "NavigationViewModel.h"

#include <QApplication>
#include <QStandardPaths>

#include <filesystem>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    CompositionRoot compositionRoot;
    auto navigationViewModel = compositionRoot.createNavigationViewModel();

    MainWindow mainWindow(navigationViewModel.get());
    mainWindow.show();

    const auto homePath = std::filesystem::path(
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdWString());
    navigationViewModel->navigateTo(homePath);

    return app.exec();
}
