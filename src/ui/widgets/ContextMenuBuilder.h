#pragma once

#include <cstdint>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QWidget;
struct ContextMenuEntry;

// The one place that converts std::vector<ContextMenuEntry> (Architecture.md §14.13) plus a set of
// already-wired fixed native QActions into an actual QMenu*: nested QMenus for submenu entries,
// QIcon from each ContextMenuIcon's RGBA buffer, separators/disabled state from isSeparator/
// enabled. Interleaves the native actions at Explorer-conventional positions around the
// registry-sourced tree. Purely a widget-construction helper — no Application/adapter calls here.
class ContextMenuBuilder
{
public:
    // Any field left null is simply omitted from the built menu.
    struct NativeActions
    {
        QAction* open = nullptr;
        QAction* cut = nullptr;
        QAction* copy = nullptr;
        QAction* paste = nullptr;
        QAction* deleteAction = nullptr;
        QAction* rename = nullptr;
        QAction* newFolder = nullptr;
        QAction* properties = nullptr;

        // Archive browsing (Architecture.md §14.28): "Extract Here" (destination = the archive's
        // own containing folder) and "Extract to..." (folder-picker dialog). Shown either for a
        // not-yet-entered archive-file selection, or (targeting the in-archive selection instead)
        // while browsing inside one.
        QAction* extractHere = nullptr;
        QAction* extractTo = nullptr;
    };

    // Returned QMenu is parented to parent (caller does not need to delete it manually if parent
    // outlives the call site; WorkspacePaneWidget builds one per right-click and lets Qt parent-
    // delete it, same lifetime pattern as any other transient popup QMenu).
    static QMenu* buildItemMenu(const std::vector<ContextMenuEntry>& entries, const NativeActions& actions, QWidget* parent);
    static QMenu* buildBackgroundMenu(const std::vector<ContextMenuEntry>& entries, const NativeActions& actions, QWidget* parent);

    // Resolves the registry-sourced entry id a triggered QAction* corresponds to, if any (returns
    // nullopt for a native action, a submenu container, or a null/cancelled action) — the caller
    // routes a valid id to FileOperationsController::invokeContextMenuEntry, or calls
    // discardContextMenu() otherwise.
    static std::optional<std::uint32_t> entryIdForAction(const QAction* action);
};
