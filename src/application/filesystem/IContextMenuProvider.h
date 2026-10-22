#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "NativeTypes.h"
#include "Result.h"

// Screen coordinates for TrackPopupMenuEx; replaces QPoint at this Port boundary (Architecture.md
// §14.102).
struct NativeMenuPoint
{
    int x = 0;
    int y = 0;
};

// One app-native action to splice into the real shell menu at a fixed position (Architecture.md
// §14.102). `label` is UTF-8 and may contain a "&" mnemonic, same convention the old
// ContextMenuEntry::label used. `id` is caller-chosen and must stay clear of the shell's own id
// range ([1, 0x7000]) -- ShellContextMenuProvider reserves [0x7001, ...) for these.
struct NativeMenuAction
{
    std::uint32_t id = 0;
    std::string label;
    bool isSeparator = false;
    bool enabled = true;
};

// Mirrors the fixed slot layout ContextMenuBuilder hardcoded before §14.102: top (Open; Extract
// Here/Extract to), clipboard (Cut/Copy/Paste, or Paste/New Folder for the background menu),
// bottom (Delete/Rename/Bulk Rename), and final (Properties) -- spliced into the real shell HMENU
// at these same conventional positions, around whatever the shell itself contributed. A caller
// embeds its own separators (isSeparator=true entries) *within* a group where the old QMenu had
// one (e.g. between Open and the Extract actions); the adapter adds exactly one separator between
// each pair of adjacent non-empty groups.
struct NativeMenuLayout
{
    std::vector<NativeMenuAction> topActions;
    std::vector<NativeMenuAction> clipboardActions;
    std::vector<NativeMenuAction> bottomActions;
    std::vector<NativeMenuAction> finalActions;
};

enum class ContextMenuOutcome
{
    Cancelled,          // the menu was dismissed without picking anything.
    ShellCommandInvoked, // a shell-sourced item was picked and already invoked.
    AppAction,           // one of NativeMenuLayout's own actions was picked; see appActionId.
};

struct ContextMenuResult
{
    ContextMenuOutcome outcome = ContextMenuOutcome::Cancelled;
    std::optional<std::uint32_t> appActionId; // set only when outcome == AppAction.
};

// Port for showing the real native shell context menu -- the exact same IContextMenu/
// QueryContextMenu/TrackPopupMenuEx machinery Explorer itself uses to build and show its own popup
// -- merged with the app's own fixed actions from a NativeMenuLayout (Architecture.md §14.102;
// originally a registry/COM-sourced plain-data builder rendered into a separate Qt QMenu, §14.13).
// Implemented by src/adapters/shell/ShellContextMenuProvider. Building, merging, showing, and
// dispatching the menu is one blocking call -- there's no build/invoke/discard split anymore since
// nothing outlives the call.
class IContextMenuProvider
{
public:
    virtual ~IContextMenuProvider() = default;

    // paths must all share one parent directory (Explorer never right-clicks a cross-folder
    // selection). A vector today even though only a single-item selection is ever passed (selection
    // stays single-item for this increment) — extending to multi-select later is a caller-side
    // change.
    virtual Result<ContextMenuResult> showItemMenu(const std::vector<std::filesystem::path>& paths,
                                                     const NativeMenuLayout& layout, NativeWindowHandle ownerWindow,
                                                     NativeMenuPoint screenPos) = 0;
    virtual Result<ContextMenuResult> showBackgroundMenu(const std::filesystem::path& folder,
                                                           const NativeMenuLayout& layout, NativeWindowHandle ownerWindow,
                                                           NativeMenuPoint screenPos) = 0;
};
