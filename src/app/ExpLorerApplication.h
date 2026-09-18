#pragma once

#include <QApplication>

// Thin QApplication subclass whose sole purpose is to catch exceptions thrown out of Qt event
// dispatch (input events, paint events, queued slot invocations) and route them to
// ExceptionHandler::handleFatal instead of letting them propagate out of exec() as a crash.
// Architecture.md §14.22.
class ExpLorerApplication : public QApplication
{
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override;
};
