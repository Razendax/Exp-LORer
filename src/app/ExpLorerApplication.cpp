#include "ExpLorerApplication.h"

#include "ExceptionHandler.h"

bool ExpLorerApplication::notify(QObject* receiver, QEvent* event)
{
    try
    {
        return QApplication::notify(receiver, event);
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
