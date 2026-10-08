#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "NativeTypes.h"
#include "Result.h"

enum class ContextMenuSourceMode
{
    StaticVerbsOnly,
    StaticAndShellExtensions,
};

// Plain bitmap, no Qt — converted to QIcon only in src/ui/widgets.
struct ContextMenuIcon
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width*height*4, empty => no icon resolved
};

struct ContextMenuEntry
{
    std::uint32_t id = 0;                  // opaque; valid only against the build that produced it
    std::string label;                     // UTF-8; may contain a "&" mnemonic, same as QAction
    bool isSeparator = false;
    bool enabled = true;
    ContextMenuIcon icon;
    std::vector<ContextMenuEntry> submenu; // non-empty => this entry is a submenu container
};

// Port for building a custom, registry-sourced file/folder context menu's *content* and invoking
// whichever entry the user picks (Architecture.md §14.13) — not for displaying the OS's own popup;
// the popup itself is a Qt-drawn QMenu built by src/ui/widgets/ContextMenuBuilder from the data
// returned here. Implemented by src/adapters/shell/ShellContextMenuProvider.
class IContextMenuProvider
{
public:
    virtual ~IContextMenuProvider() = default;

    // Registry-sourced entries only (Cut/Copy/Paste/Delete/Rename/Properties/default-Open never
    // appear here — the UI layer injects app-native actions for those at fixed positions instead).
    // paths must all share one parent directory (Explorer never right-clicks a cross-folder
    // selection). A vector today even though only a single-item selection is ever passed (selection
    // stays single-item for this increment) — extending to multi-select later is a caller-side
    // change.
    virtual Result<std::vector<ContextMenuEntry>> buildItemMenu(const std::vector<std::filesystem::path>& paths,
                                                                  ContextMenuSourceMode mode) = 0;
    virtual Result<std::vector<ContextMenuEntry>> buildBackgroundMenu(const std::filesystem::path& folder,
                                                                       ContextMenuSourceMode mode) = 0;

    // Executes the entry with this id from the most recent buildItemMenu/buildBackgroundMenu call
    // (static: expands and runs its command line; dynamic: calls InvokeCommand on the specific COM
    // handler that produced it).
    virtual Result<void> invoke(std::uint32_t entryId, NativeWindowHandle ownerWindow) = 0;

    // Releases any COM handlers kept alive by the last build call when the user closes the menu
    // without picking a registry-sourced entry. Always safe to call (no-op if nothing pending).
    virtual void discardMenu() = 0;
};
