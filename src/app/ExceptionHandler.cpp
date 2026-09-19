#include "ExceptionHandler.h"

#include <array>
#include <cstdlib>
#include <sstream>

#include <QMessageBox>
#include <QString>

#include <Windows.h>

#include <DbgHelp.h>

#include "Logging.h"

namespace
{
    // Best-effort symbolized call stack of the current thread, formatted one frame per line. Since
    // handleFatal runs from a catch block, this is the unwound stack at the catch site (main()'s
    // top-level try, or ExpLorerApplication::notify()) rather than the original throw location — the
    // deeper frames that actually threw have already unwound. Still useful context alongside the
    // exception message, and symbol resolution degrades gracefully (falls back to a bare address) if
    // no PDB is available (e.g. a Release build without symbols).
    std::string captureCallStack()
    {
        constexpr USHORT maxFrames = 64;
        // Skip this function's own frame.
        constexpr USHORT framesToSkip = 1;

        std::array<void*, maxFrames> addresses{};
        const USHORT captured = CaptureStackBackTrace(framesToSkip, maxFrames, addresses.data(), nullptr);
        if (captured == 0)
        {
            return "  (no call stack available)";
        }

        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        const BOOL symbolsInitialized = SymInitialize(process, nullptr, TRUE);

        std::ostringstream stack;
        constexpr size_t maxNameLength = 256;
        std::array<char, sizeof(SYMBOL_INFO) + maxNameLength> symbolBuffer{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = maxNameLength;

        for (USHORT i = 0; i < captured; ++i)
        {
            const auto address = reinterpret_cast<DWORD64>(addresses[i]);
            stack << "  #" << i << " 0x" << std::hex << address << std::dec;

            DWORD64 displacement = 0;
            if (symbolsInitialized && SymFromAddr(process, address, &displacement, symbol))
            {
                stack << " " << symbol->Name << " + 0x" << std::hex << displacement << std::dec;

                DWORD lineDisplacement = 0;
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
                {
                    stack << " (" << line.FileName << ":" << line.LineNumber << ")";
                }
            }

            if (i + 1 < captured)
            {
                stack << "\n";
            }
        }

        if (symbolsInitialized)
        {
            SymCleanup(process);
        }

        return stack.str();
    }
}

void ExceptionHandler::handleFatal(const std::string& message)
{
    Logging::error(message + "\nCall stack:\n" + captureCallStack());
    Logging::shutdown();

    QMessageBox messageBox;
    messageBox.setIcon(QMessageBox::Critical);
    messageBox.setWindowTitle(QObject::tr("Unexpected Error"));
    messageBox.setText(QString::fromStdString(message));
    messageBox.addButton(QObject::tr("Exit"), QMessageBox::AcceptRole);
    messageBox.exec();

    std::exit(1);
}
