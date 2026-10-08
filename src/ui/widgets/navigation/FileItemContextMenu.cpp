#include "FileItemContextMenu.h"

#include <algorithm>

#include <QAction>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QWidget>

#include "ArchiveExtensions.h"
#include "ContextMenuBuilder.h"
#include "FileOperationsController.h"
#include "IContextMenuProvider.h"
#include "WorkspacePaneWidget.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    bool extendedShellExtensionsEnabled()
    {
        QSettings settings;
        return settings.value(QLatin1String(WorkspacePaneWidget::kShowShellExtensionsSettingsKey), false).toBool();
    }
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

    auto* openAction = new QAction(QObject::tr("Open"));
    auto* cutAction = new QAction(QObject::tr("Cut"));
    auto* copyAction = new QAction(QObject::tr("Copy"));
    auto* pasteAction = new QAction(QObject::tr("Paste"));
    auto* deleteAction = new QAction(QObject::tr("Delete"));
    auto* renameAction = new QAction(QObject::tr("Rename"));
    auto* bulkRenameAction = new QAction(QObject::tr("Bulk Rename..."));
    auto* propertiesAction = new QAction(QObject::tr("Properties"));
    auto* extractHereAction = new QAction(QObject::tr("Extract Here"));
    auto* extractToAction = new QAction(browsingInsideArchive ? QObject::tr("Extract...") : QObject::tr("Extract to..."));

    QObject::connect(openAction, &QAction::triggered, parent, [controller, targetPath, open = callbacks.open]() {
        if (open)
        {
            open(targetPath);
        }
        else
        {
            controller->openFile(targetPath);
        }
    });
    QObject::connect(cutAction, &QAction::triggered, parent, [controller, paths]() { controller->cutToClipboard(paths); });
    QObject::connect(copyAction, &QAction::triggered, parent, [controller, paths]() { controller->copyToClipboard(paths); });
    QObject::connect(pasteAction, &QAction::triggered, parent, [controller, directory]() { controller->pasteInto(directory); });
    if (callbacks.deleteItems)
    {
        QObject::connect(deleteAction, &QAction::triggered, parent, callbacks.deleteItems);
    }
    if (callbacks.rename)
    {
        QObject::connect(renameAction, &QAction::triggered, parent, callbacks.rename);
    }
    if (callbacks.bulkRename)
    {
        QObject::connect(bulkRenameAction, &QAction::triggered, parent, callbacks.bulkRename);
    }
    QObject::connect(propertiesAction, &QAction::triggered, parent, [controller, targetPath, ownerWindow]() {
        controller->showProperties(targetPath, ownerWindow);
    });
    QObject::connect(extractHereAction, &QAction::triggered, parent, [controller, paths]() {
        for (const std::filesystem::path& archiveFile : paths)
        {
            controller->extractArchive(archiveFile, archiveFile.parent_path() / archiveFile.stem());
        }
    });
    QObject::connect(extractToAction, &QAction::triggered, parent, [parent, controller, paths]() {
        const QString chosen = QFileDialog::getExistingDirectory(parent, QObject::tr("Extract to"));
        if (chosen.isEmpty())
        {
            return;
        }
        const std::filesystem::path destination(chosen.toStdWString());

        if (paths.size() == 1)
        {
            controller->extractArchive(paths.front(), destination);
            return;
        }

        // Multiple sources sharing one destination: extract each into its own subfolder named
        // after the source's own filename (unique -- paths are siblings from the same directory
        // listing) instead of flattening them all directly into destination, where same-named
        // entries from different sources would silently overwrite each other.
        for (const std::filesystem::path& source : paths)
        {
            controller->extractArchive(source, destination / source.filename());
        }
    });

    // Rename/Properties/Open are single-target-only (rename-as-move, showProperties, and
    // openFile/itemActivated all take one path) — disable rather than guess a multi-item
    // behavior when more than one item is selected (Architecture.md §14.13.2/§14.13.6).
    const bool singleTarget = paths.size() == 1;
    openAction->setEnabled(singleTarget);
    renameAction->setEnabled(singleTarget && !browsingInsideArchive && callbacks.rename);
    propertiesAction->setEnabled(singleTarget && !browsingInsideArchive);

    // Bulk Rename only makes sense for a multi-item selection (Architecture.md §14.55) -- single-
    // item rename stays F2/"Rename" above, same precedent as Cut/Copy/Delete already acting on the
    // whole selection.
    bulkRenameAction->setEnabled(paths.size() > 1 && !browsingInsideArchive && callbacks.bulkRename);

    // A read-only, virtual archive location has no real CF_HDROP paths for the OS clipboard, and
    // Cut/Paste/Delete/New Folder/Properties aren't meaningful for it either (Architecture.md
    // §14.28); the UI disables these proactively even though StandardFileSystemRepository also
    // refuses them defensively.
    cutAction->setEnabled(!browsingInsideArchive);
    copyAction->setEnabled(!browsingInsideArchive);
    pasteAction->setEnabled(!browsingInsideArchive);
    deleteAction->setEnabled(!browsingInsideArchive && callbacks.deleteItems);

    ContextMenuBuilder::NativeActions actions;
    actions.open = openAction;
    actions.cut = cutAction;
    actions.copy = copyAction;
    actions.paste = pasteAction;
    actions.deleteAction = deleteAction;
    actions.rename = renameAction;
    actions.bulkRename = bulkRenameAction;
    actions.properties = propertiesAction;
    if (selectionIsUnenteredArchives)
    {
        actions.extractHere = extractHereAction;
        actions.extractTo = extractToAction;
    }
    else if (browsingInsideArchive)
    {
        actions.extractTo = extractToAction;
    }

    const ContextMenuSourceMode mode =
        extendedShellExtensionsEnabled() ? ContextMenuSourceMode::StaticAndShellExtensions : ContextMenuSourceMode::StaticVerbsOnly;
    auto buildResult = controller->buildContextMenuForSelection(paths, mode);

    std::vector<ContextMenuEntry> entries;
    if (buildResult.hasValue())
    {
        entries = std::move(buildResult).value();
    }
    else if (callbacks.reportError)
    {
        callbacks.reportError(QString::fromStdString(buildResult.error().message));
    }

    QMenu* menu = ContextMenuBuilder::buildItemMenu(entries, actions, parent);
    QAction* chosen = menu->exec(globalPos);

    const auto entryId = chosen ? ContextMenuBuilder::entryIdForAction(chosen) : std::nullopt;
    if (entryId)
    {
        controller->invokeContextMenuEntry(*entryId, directory, ownerWindow);
    }
    else
    {
        controller->discardContextMenu();
    }

    menu->deleteLater();
    for (QAction* action : { openAction, cutAction, copyAction, pasteAction, deleteAction, renameAction, bulkRenameAction,
                              propertiesAction, extractHereAction, extractToAction })
    {
        action->deleteLater();
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
