#include "CompositionRoot.h"
#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    CompositionRoot compositionRoot;

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
