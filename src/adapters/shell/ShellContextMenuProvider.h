#pragma once

#include "IContextMenuProvider.h"

// Implements IContextMenuProvider by calling the shell's own IContextMenu/QueryContextMenu/
// TrackPopupMenuEx machinery -- the same calls Explorer itself makes to build and show its own
// popup (Architecture.md §14.102) -- merged with the app's own fixed actions from a
// NativeMenuLayout. Named after the StandardFileSystemRepository pattern (platform-general name,
// #ifdef _WIN32 internals) rather than a Windows-specific class name, so CompositionRoot wiring
// doesn't need to change when a Linux implementation eventually lands behind the same Port. Must
// only ever be called from the Qt UI thread (no explicit CoInitializeEx -- this assumes COM STA is
// already initialized by Qt's Windows platform plugin, same assumption ShellExecuteW/
// SHFileOperationW elsewhere in the codebase already make).
class ShellContextMenuProvider : public IContextMenuProvider
{
public:
    ShellContextMenuProvider() = default;
    ~ShellContextMenuProvider() override = default;

    Result<ContextMenuResult> showItemMenu(const std::vector<std::filesystem::path>& paths, const NativeMenuLayout& layout,
                                             NativeWindowHandle ownerWindow, NativeMenuPoint screenPos) override;
    Result<ContextMenuResult> showBackgroundMenu(const std::filesystem::path& folder, const NativeMenuLayout& layout,
                                                   NativeWindowHandle ownerWindow, NativeMenuPoint screenPos) override;
};
