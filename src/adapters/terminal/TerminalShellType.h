#pragma once

#include <QString>

// Which shell a TerminalWidget/WindowsConPtyProcess session launches (Architecture.md §14.24).
enum class TerminalShellType
{
    Cmd,
    PowerShell
};

inline QString terminalShellDisplayName(TerminalShellType type)
{
    return type == TerminalShellType::PowerShell ? QStringLiteral("PowerShell") : QStringLiteral("cmd");
}
