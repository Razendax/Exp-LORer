#pragma once

#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "IContextMenuProvider.h"

class FileNavigationUseCase;

// Single shared instance (pane-agnostic; the OS clipboard is process/OS-global, unlike
// TagListViewModel there is no "active tab" retargeting), owned by WorkspaceController
// (Architecture.md §14.10). Drives Ctrl+C/X/V and Delete/Shift+Delete against the real Windows
// clipboard (CF_HDROP file URLs + "Preferred DropEffect") so cut/copy/paste interoperate with
// File Explorer and other apps.
class FileOperationsController : public QObject
{
    Q_OBJECT

public:
    explicit FileOperationsController(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);

public slots:
    void copyToClipboard(const std::filesystem::path& source);
    void cutToClipboard(const std::filesystem::path& source);

    // Reads the OS clipboard's file URLs and Preferred-DropEffect flag; copies (plain clipboard)
    // or moves (cut clipboard) each into destinationDirectory, auto-renaming on collision. Clears
    // the clipboard afterwards if it was a cut. Best-effort across multiple URLs (interop with a
    // multi-file Explorer clipboard): failures are collected into one operationFailed message
    // rather than aborting the whole paste.
    void pasteInto(const std::filesystem::path& destinationDirectory);

    void moveToTrash(const std::filesystem::path& path);
    void deletePermanently(const std::filesystem::path& path);
    void openFile(const std::filesystem::path& path);

    // Shows the real OS shell context menu (Architecture.md §14.13) and, on success, refreshes
    // the affected directory unconditionally — the shell's chosen verb isn't inspected, so this
    // is correct whether the user picked Rename/Delete/Paste/a shell-extension action, or
    // cancelled (cancelling is also a "success" from the Port's point of view, just a no-op).
    void showContextMenuForSelection(const std::vector<std::filesystem::path>& paths, NativeScreenPoint screenPosition,
                                      NativeWindowHandle ownerWindow);
    void showContextMenuForFolder(const std::filesystem::path& folder, NativeScreenPoint screenPosition,
                                   NativeWindowHandle ownerWindow);

signals:
    // Mirrors TabViewModel::navigationFailed's pattern for status-bar reporting.
    void operationFailed(const QString& message);

    // Emitted after any successful copy/move/paste/delete with the affected directory (the
    // destination for a paste, the source's parent for a cut-paste or delete). WorkspaceController
    // refreshes any live tab whose currentPath() matches.
    void directoryContentsMayHaveChanged(const std::filesystem::path& directory);

private:
    // Appends " (2)", " (3)", ... before the extension until destinationDirectory/candidate
    // doesn't exist, checked via FileNavigationUseCase::stat (NotFound => free) rather than
    // std::filesystem directly, keeping existence checks behind the Port like everywhere else.
    std::filesystem::path uniqueDestinationName(const std::filesystem::path& destinationDirectory,
                                                 const std::filesystem::path& desiredName) const;

    FileNavigationUseCase& m_fileNavigationUseCase;
};
