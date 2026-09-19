#pragma once

#include <string>

// Last-resort handler for a C++ exception that reaches the top of the call stack (either before
// the Qt event loop starts, or from within Qt event dispatch via ExpLorerApplication::notify).
// Architecture.md §14.22.
namespace ExceptionHandler
{
    // Logs the message at Error level, along with a best-effort symbolized call stack captured at
    // the catch site (DbgHelp), then shows a modal dialog (title "Unexpected Error", the message
    // only, a single "Exit" button), then terminates the process (std::exit(1)) once dismissed.
    // Never returns.
    [[noreturn]] void handleFatal(const std::string& message);
}
