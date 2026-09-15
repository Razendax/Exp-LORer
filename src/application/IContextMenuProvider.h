#pragma once

#include <filesystem>
#include <vector>

#include "Result.h"

// Opaque native types only, so this Port stays framework-agnostic — same boundary precedent as
// std::filesystem::path already crossing into Application (Architecture.md §14.13).
using NativeWindowHandle = void*;

struct NativeScreenPoint
{
    int x = 0;
    int y = 0;
};

// Port for showing/invoking the OS's own file/folder context menu (Architecture.md §14.13).
// Distinct from IFileSystemRepository: this concern needs a screen position and a native owner
// window handle, and blocks the calling thread in a nested native message loop while the menu is
// open. Implemented by src/adapters/shell/ShellContextMenuProvider.
class IContextMenuProvider
{
public:
    virtual ~IContextMenuProvider() = default;

    // paths must all share one parent directory (Explorer never right-clicks a cross-folder
    // selection). Maps to IShellFolder::GetUIObjectOf(..., IID_IContextMenu, ...) on the parent.
    // A vector today even though only a single-item selection is ever passed (selection stays
    // single-item for this increment) — extending to multi-select later is a caller-side change.
    virtual Result<void> showItemContextMenu(const std::vector<std::filesystem::path>& paths,
                                              NativeScreenPoint screenPosition,
                                              NativeWindowHandle ownerWindow) = 0;

    // Maps to IShellFolder::CreateViewObject(hwnd, IID_IContextMenu, ...) on the folder itself
    // (gives New/Paste/Sort by/Refresh/folder-Properties — not interchangeable with the per-item
    // object above).
    virtual Result<void> showBackgroundContextMenu(const std::filesystem::path& folder,
                                                     NativeScreenPoint screenPosition,
                                                     NativeWindowHandle ownerWindow) = 0;
};
