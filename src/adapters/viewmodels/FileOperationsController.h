#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <QObject>
#include <QString>

#include "IContextMenuProvider.h"
#include "NativeTypes.h"

class FileNavigationUseCase;
class FileTransferService;
class NotificationCenter;

// Single shared instance (pane-agnostic; the OS clipboard is process/OS-global, unlike
// TagListViewModel there is no "active tab" retargeting), owned by WorkspaceController
// (Architecture.md §14.10). Drives Ctrl+C/X/V and Delete/Shift+Delete against the real Windows
// clipboard (CF_HDROP file URLs + "Preferred DropEffect") so cut/copy/paste interoperate with
// File Explorer and other apps.
class FileOperationsController : public QObject
{
    Q_OBJECT

public:
    FileOperationsController(FileNavigationUseCase& fileNavigationUseCase, FileTransferService& fileTransferService,
                              NotificationCenter& notificationCenter, QObject* parent = nullptr);

public slots:
    void copyToClipboard(const std::vector<std::filesystem::path>& sources);
    void cutToClipboard(const std::vector<std::filesystem::path>& sources);

    // Reads the OS clipboard's file URLs and Preferred-DropEffect flag; copies (plain clipboard)
    // or moves (cut clipboard) each into destinationDirectory, auto-renaming on collision. Clears
    // the clipboard afterwards if it was a cut.
    //
    // A move is first attempted as a direct, instant FileNavigationUseCase::moveFile (same-volume
    // rename) for each URL; only URLs where that fails (cross-volume) plus every copy-paste URL
    // (which always has to duplicate bytes) are batched into one FileTransferService transfer
    // driving a cancellable progress notification -- the estimate -> free-space-check -> copy ->
    // completion-toast pipeline of Architecture.md §14.47. If every URL resolves via the instant
    // path, no progress notification is created at all (same instant behavior as before this
    // feature). The batch's errors surface as a single operationFailed message (or, for
    // insufficient space, insufficientDiskSpace below) rather than per-file messages.
    void pasteInto(const std::filesystem::path& destinationDirectory);

    void moveToTrash(const std::filesystem::path& path);
    void deletePermanently(const std::filesystem::path& path);
    void openFile(const std::filesystem::path& path);

    // Read-only archive extraction (Architecture.md §14.28) -- backs both "Extract Here"/
    // "Extract to..." on a not-yet-entered archive file and "Extract..." on a selection inside one.
    void extractArchive(const std::filesystem::path& archiveFile, const std::filesystem::path& destinationDirectory);

    // Rename is just a same-directory move (Architecture.md §14.13) — no new Port method needed.
    void renamePath(const std::filesystem::path& source, const std::filesystem::path& destination);

    // Applies every (source, destination) pair in order via the same FileNavigationUseCase::moveFile
    // single-item call renamePath() already uses (Architecture.md §14.55). Emits pathRenamed() per
    // successful item -- so WorkspaceController's existing per-rename tab/tree repoint logic
    // (§14.41) runs unchanged -- then one directoryContentsMayHaveChanged() for the shared parent
    // directory at the end, batched, same posture as pasteInto's single aggregated operationFailed
    // for a multi-item batch. An entry that fails doesn't stop the rest; if any failed, one combined
    // operationFailed (naming the count and the first failure's message) is emitted at the end.
    void bulkRename(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& renames);

    // Builds the registry-sourced entries for a custom QMenu (Architecture.md §14.13); does not
    // touch QMenu/QAction itself — src/ui/widgets/ContextMenuBuilder does that.
    Result<std::vector<ContextMenuEntry>> buildContextMenuForSelection(const std::vector<std::filesystem::path>& paths,
                                                                        ContextMenuSourceMode mode);
    Result<std::vector<ContextMenuEntry>> buildContextMenuForFolder(const std::filesystem::path& folder, ContextMenuSourceMode mode);

    // Invokes the chosen registry-sourced entry and, on success, unconditionally refreshes
    // directory — we can't know what a registry/COM-invoked entry did to disk, so refresh-on-any-
    // success is the simple correct default, same posture the paste/delete slots already use.
    void invokeContextMenuEntry(std::uint32_t entryId, const std::filesystem::path& directory, NativeWindowHandle ownerWindow);
    void discardContextMenu();

    // Returns the created folder's path (a de-duplicated "New folder", "New folder (2)", ...) on
    // success so the caller can immediately follow up with the Rename dialog — the practical
    // equivalent of Explorer's inline rename-on-create — or nullopt on failure (operationFailed is
    // still emitted in that case).
    std::optional<std::filesystem::path> createFolder(const std::filesystem::path& parentDirectory);
    void createFileFromTemplate(const std::filesystem::path& destinationFile,
                                 const std::optional<std::filesystem::path>& templateFile);
    void showProperties(const std::filesystem::path& path, NativeWindowHandle ownerWindow);

signals:
    // Mirrors TabViewModel::navigationFailed's pattern for status-bar reporting.
    void operationFailed(const QString& message);

    // Emitted by pasteInto() when FileTransferUseCase::checkDestinationHasSpace fails for a batch
    // that needed real byte-copying (Architecture.md §14.47) -- no file is copied in that case.
    // MainWindow raises a blocking QMessageBox for it, parallel to operationFailed's status-bar
    // channel, since this failure happens before any progress notification would otherwise explain
    // why the paste did nothing.
    void insufficientDiskSpace(const QString& message);

    // Emitted after any successful copy/move/paste/delete with the affected directory (the
    // destination for a paste, the source's parent for a cut-paste or delete). WorkspaceController
    // refreshes any live tab whose currentPath() matches.
    void directoryContentsMayHaveChanged(const std::filesystem::path& directory);

    // Emitted by renamePath() on success, just before directoryContentsMayHaveChanged(source's
    // parent). WorkspaceController re-points every tab browsing at/inside `source` to the matching
    // path under `destination`, and renames the folder tree's node (Architecture.md §14.41).
    void pathRenamed(const std::filesystem::path& source, const std::filesystem::path& destination);

    // Emitted by moveToTrash()/deletePermanently() on success, just before
    // directoryContentsMayHaveChanged(path's parent). WorkspaceController moves every tab browsing
    // at/inside `path` up to its parent (Architecture.md §14.41).
    void pathRemoved(const std::filesystem::path& path);

private:
    // Appends " (2)", " (3)", ... before the extension until destinationDirectory/candidate
    // doesn't exist, checked via FileNavigationUseCase::stat (NotFound => free) rather than
    // std::filesystem directly, keeping existence checks behind the Port like everywhere else.
    std::filesystem::path uniqueDestinationName(const std::filesystem::path& destinationDirectory,
                                                 const std::filesystem::path& desiredName) const;

    FileNavigationUseCase& m_fileNavigationUseCase;
    FileTransferService& m_fileTransferService;
    NotificationCenter& m_notificationCenter;
};
