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

    // Rename is just a same-directory move (Architecture.md §14.13) — no new Port method needed.
    void renamePath(const std::filesystem::path& source, const std::filesystem::path& destination);

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
