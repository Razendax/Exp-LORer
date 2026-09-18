#include "ExceptionHandler.h"

#include <cstdlib>

#include <QMessageBox>
#include <QString>

#include "Logging.h"

void ExceptionHandler::handleFatal(const std::string& message)
{
    Logging::error(message);
    Logging::shutdown();

    QMessageBox messageBox;
    messageBox.setIcon(QMessageBox::Critical);
    messageBox.setWindowTitle(QObject::tr("Unexpected Error"));
    messageBox.setText(QString::fromStdString(message));
    messageBox.addButton(QObject::tr("Exit"), QMessageBox::AcceptRole);
    messageBox.exec();

    std::exit(1);
}
