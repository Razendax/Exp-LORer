#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <QPoint>
#include <QString>

class QWidget;
class FileOperationsController;

// The file/folder *item* context menu (Open/Cut/Copy/Paste/Delete/Rename/Properties/Extract plus
// the registry-sourced entries, Architecture.md §14.13/§14.28) and the confirm-then-delete flow,
// shared by the main file view (WorkspacePaneWidget) and the left panel's folder tree
// (Architecture.md §14.41). Same widget-construction-helper posture as ContextMenuBuilder: static
// functions only, no state of its own.
class FileItemContextMenu
{
public:
    // Caller-specific behavior. A null rename/deleteItems disables that action.
    struct Callbacks
    {
        std::function<void()> rename;
        std::function<void()> deleteItems;

        // Receives a failure to build the registry-sourced part of the menu (the fixed actions are
        // still shown).
        std::function<void(const QString&)> reportError;

        // Replaces "Open"'s default (FileOperationsController::openFile) when set -- the folder
        // tree opens a folder by navigating the focused tab to it.
        std::function<void(const std::filesystem::path&)> open;
    };

    // Builds, runs (modal QMenu::exec) and tears down the menu for `paths` at `globalPos`.
    // No-op for an empty `paths`. `browsingInsideArchive` switches to the read-only archive action
    // set (§14.28).
    static void show(QWidget* parent, FileOperationsController& fileOperationsController,
                     const std::vector<std::filesystem::path>& paths, bool browsingInsideArchive, const Callbacks& callbacks,
                     const QPoint& globalPos);

    // Asks "Move to the Recycle Bin?" / "Permanently delete?" for `paths` and, on Yes, deletes
    // each through FileOperationsController. No-op for an empty `paths`.
    static void confirmAndDelete(QWidget* parent, FileOperationsController& fileOperationsController,
                                 const std::vector<std::filesystem::path>& paths, bool permanent);
};
