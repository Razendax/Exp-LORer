#pragma once

#include "IContextMenuProvider.h"

// Implements IContextMenuProvider using the Windows Shell COM APIs (IShellFolder/IContextMenu),
// Architecture.md §14.13. Named after the StandardFileSystemRepository pattern (platform-general
// name, #ifdef _WIN32 internals) rather than a Windows-specific class name, so CompositionRoot
// wiring doesn't need to change when a Linux implementation eventually lands behind the same Port.
// Must only ever be called from the Qt UI thread (no explicit CoInitializeEx — this assumes COM
// STA is already initialized by Qt's Windows platform plugin, same assumption ShellExecuteW/
// SHFileOperationW elsewhere in the codebase already make).
class ShellContextMenuProvider : public IContextMenuProvider
{
public:
    Result<void> showItemContextMenu(const std::vector<std::filesystem::path>& paths,
                                      NativeScreenPoint screenPosition, NativeWindowHandle ownerWindow) override;
    Result<void> showBackgroundContextMenu(const std::filesystem::path& folder, NativeScreenPoint screenPosition,
                                            NativeWindowHandle ownerWindow) override;
};
