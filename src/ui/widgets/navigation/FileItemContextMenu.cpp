#include "FileItemContextMenu.h"

#include <algorithm>

#include <QFileDialog>
#include <QMessageBox>
#include <QWidget>

#include "ArchiveExtensions.h"
#include "FileOperationsController.h"
#include "IContextMenuProvider.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // Fixed ids for this menu's app-native actions (Architecture.md §14.102) -- valid only for the
    // one showItemContextMenu call below, clear of the shell's own [1, 0x7000] id range.
    constexpr std::uint32_t kOpenActionId = 0x7001;
    constexpr std::uint32_t kExtractHereActionId = 0x7002;
    constexpr std::uint32_t kExtractToActionId = 0x7003;
    constexpr std::uint32_t kCutActionId = 0x7004;
    constexpr std::uint32_t kCopyActionId = 0x7005;
    constexpr std::uint32_t kPasteActionId = 0x7006;
    constexpr std::uint32_t kDeleteActionId = 0x7007;
    constexpr std::uint32_t kRenameActionId = 0x7008;
    constexpr std::uint32_t kBulkRenameActionId = 0x7009;
    constexpr std::uint32_t kPropertiesActionId = 0x700A;
}

void FileItemContextMenu::show(QWidget* parent, FileOperationsController& fileOperationsController,
                               const std::vector<std::filesystem::path>& paths, bool browsingInsideArchive,
                               const Callbacks& callbacks, const QPoint& globalPos)
{
    if (paths.empty())
    {
        return;
    }

    FileOperationsController* controller = &fileOperationsController;
    const auto ownerWindow = reinterpret_cast<NativeWindowHandle>(parent->window()->winId());
    const std::filesystem::path directory = paths.front().parent_path();
    const std::filesystem::path targetPath = paths.front();

    // Archive browsing (Architecture.md §14.28): a lexical hint only (ArchiveExtensions), the
    // adapter layer re-verifies against real disk state before doing anything -- a mismatch here
    // just shows/hides the wrong menu items, never crashes.
    const bool selectionIsUnenteredArchives =
        !browsingInsideArchive &&
        std::all_of(paths.begin(), paths.end(),
                    [](const std::filesystem::path& p) { return ArchiveExtensions::archiveAncestorInPath(p) == p; });

    // Rename/Properties/Open are single-target-only (rename-as-move, showProperties, and
    // openFile/itemActivated all take one path) — disable rather than guess a multi-item
    // behavior when more than one item is selected (Architecture.md §14.13.2/§14.13.6).
    const bool singleTarget = paths.size() == 1;

    // Bulk Rename only makes sense for a multi-item selection (Architecture.md §14.55) -- single-
    // item rename stays F2/"Rename" above, same precedent as Cut/Copy/Delete already acting on the
    // whole selection.
    const bool bulkRenameEnabled = paths.size() > 1 && !browsingInsideArchive && static_cast<bool>(callbacks.bulkRename);

    NativeMenuLayout layout;

    NativeMenuAction openAction;
    openAction.id = kOpenActionId;
    openAction.label = QObject::tr("Open").toStdString();
    openAction.enabled = singleTarget;
    layout.topActions.push_back(openAction);

    NativeMenuAction topSeparator;
    topSeparator.isSeparator = true;
    layout.topActions.push_back(topSeparator);

    if (selectionIsUnenteredArchives || browsingInsideArchive)
    {
        if (selectionIsUnenteredArchives)
        {
            NativeMenuAction extractHereAction;
            extractHereAction.id = kExtractHereActionId;
            extractHereAction.label = QObject::tr("Extract Here").toStdString();
            layout.topActions.push_back(extractHereAction);
        }

        NativeMenuAction extractToAction;
        extractToAction.id = kExtractToActionId;
        extractToAction.label = (browsingInsideArchive ? QObject::tr("Extract...") : QObject::tr("Extract to...")).toStdString();
        layout.topActions.push_back(extractToAction);
    }

    // A read-only, virtual archive location has no real CF_HDROP paths for the OS clipboard, and
    // Cut/Paste/Delete/Properties aren't meaningful for it either (Architecture.md §14.28); the UI
    // disables these proactively even though StandardFileSystemRepository also refuses them
    // defensively.
    NativeMenuAction cutAction;
    cutAction.id = kCutActionId;
    cutAction.label = QObject::tr("Cut").toStdString();
    cutAction.enabled = !browsingInsideArchive;
    layout.clipboardActions.push_back(cutAction);

    NativeMenuAction copyAction;
    copyAction.id = kCopyActionId;
    copyAction.label = QObject::tr("Copy").toStdString();
    copyAction.enabled = !browsingInsideArchive;
    layout.clipboardActions.push_back(copyAction);

    NativeMenuAction pasteAction;
    pasteAction.id = kPasteActionId;
    pasteAction.label = QObject::tr("Paste").toStdString();
    pasteAction.enabled = !browsingInsideArchive;
    layout.clipboardActions.push_back(pasteAction);

    NativeMenuAction deleteAction;
    deleteAction.id = kDeleteActionId;
    deleteAction.label = QObject::tr("Delete").toStdString();
    deleteAction.enabled = !browsingInsideArchive && static_cast<bool>(callbacks.deleteItems);
    layout.bottomActions.push_back(deleteAction);

    NativeMenuAction renameAction;
    renameAction.id = kRenameActionId;
    renameAction.label = QObject::tr("Rename").toStdString();
    renameAction.enabled = singleTarget && !browsingInsideArchive && static_cast<bool>(callbacks.rename);
    layout.bottomActions.push_back(renameAction);

    NativeMenuAction bulkRenameAction;
    bulkRenameAction.id = kBulkRenameActionId;
    bulkRenameAction.label = QObject::tr("Bulk Rename...").toStdString();
    bulkRenameAction.enabled = bulkRenameEnabled;
    layout.bottomActions.push_back(bulkRenameAction);

    NativeMenuAction propertiesAction;
    propertiesAction.id = kPropertiesActionId;
    propertiesAction.label = QObject::tr("Properties").toStdString();
    propertiesAction.enabled = singleTarget && !browsingInsideArchive;
    layout.finalActions.push_back(propertiesAction);

    const NativeMenuPoint screenPos{ globalPos.x(), globalPos.y() };
    auto showResult = controller->showItemContextMenu(paths, layout, ownerWindow, screenPos);

    if (showResult.hasError())
    {
        if (callbacks.reportError)
        {
            callbacks.reportError(QString::fromStdString(showResult.error().message));
        }
        return;
    }

    const ContextMenuResult& result = showResult.value();
    if (result.outcome != ContextMenuOutcome::AppAction || !result.appActionId)
    {
        return;
    }

    switch (*result.appActionId)
    {
        case kOpenActionId:
            if (callbacks.open)
            {
                callbacks.open(targetPath);
            }
            else
            {
                controller->openFile(targetPath);
            }
            break;
        case kCutActionId:
            controller->cutToClipboard(paths);
            break;
        case kCopyActionId:
            controller->copyToClipboard(paths);
            break;
        case kPasteActionId:
            controller->pasteInto(directory);
            break;
        case kDeleteActionId:
            if (callbacks.deleteItems)
            {
                callbacks.deleteItems();
            }
            break;
        case kRenameActionId:
            if (callbacks.rename)
            {
                callbacks.rename();
            }
            break;
        case kBulkRenameActionId:
            if (callbacks.bulkRename)
            {
                callbacks.bulkRename();
            }
            break;
        case kPropertiesActionId:
            controller->showProperties(targetPath, ownerWindow);
            break;
        case kExtractHereActionId:
            for (const std::filesystem::path& archiveFile : paths)
            {
                controller->extractArchive(archiveFile, archiveFile.parent_path() / archiveFile.stem());
            }
            break;
        case kExtractToActionId:
        {
            const QString chosen = QFileDialog::getExistingDirectory(parent, QObject::tr("Extract to"));
            if (chosen.isEmpty())
            {
                break;
            }
            const std::filesystem::path destination(chosen.toStdWString());

            if (paths.size() == 1)
            {
                controller->extractArchive(paths.front(), destination);
                break;
            }

            // Multiple sources sharing one destination: extract each into its own subfolder named
            // after the source's own filename (unique -- paths are siblings from the same directory
            // listing) instead of flattening them all directly into destination, where same-named
            // entries from different sources would silently overwrite each other.
            for (const std::filesystem::path& source : paths)
            {
                controller->extractArchive(source, destination / source.filename());
            }
            break;
        }
        default:
            break;
    }
}

void FileItemContextMenu::confirmAndDelete(QWidget* parent, FileOperationsController& fileOperationsController,
                                           const std::vector<std::filesystem::path>& paths, bool permanent)
{
    if (paths.empty())
    {
        return;
    }

    const QString message = [&]() {
        if (paths.size() == 1)
        {
            const QString name = toQString(paths.front().filename());
            return permanent ? QObject::tr("Permanently delete \"%1\"? This cannot be undone.").arg(name)
                              : QObject::tr("Move \"%1\" to the Recycle Bin?").arg(name);
        }
        return permanent ? QObject::tr("Permanently delete %1 items? This cannot be undone.").arg(paths.size())
                          : QObject::tr("Move %1 items to the Recycle Bin?").arg(paths.size());
    }();

    const QMessageBox::StandardButton answer =
        QMessageBox::question(parent, permanent ? QObject::tr("Delete Permanently") : QObject::tr("Delete"), message);

    if (answer != QMessageBox::Yes)
    {
        return;
    }

    for (const std::filesystem::path& path : paths)
    {
        if (permanent)
        {
            fileOperationsController.deletePermanently(path);
        }
        else
        {
            fileOperationsController.moveToTrash(path);
        }
    }
}
