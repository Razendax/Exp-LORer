#pragma once

#include <QDialog>

#include "WorkspaceLaunchContext.h"

class QListWidget;
class QListWidgetItem;
class QPushButton;
class WorkspaceStore;

// "Workspace Management" dialog (Architecture.md §14.60), opened from File > Workspace
// Management...: a flat list of every saved workspace plus Rename/Delete/Close buttons, wired
// directly to WorkspaceStore -- same list+buttons shape as ManageFavoritesDialog/TagManagerDialog,
// just a flat QListWidget since workspaces have no category/tree structure.
class WorkspaceManagerDialog : public QDialog
{
    Q_OBJECT

public:
    // launchContext identifies the workspace this *window* is currently running as -- needed
    // because deleting it goes through the same "currently open" guard as deleting one open in
    // another window (WorkspaceInstanceProbe::isOwned reports this process's own ownership too).
    WorkspaceManagerDialog(WorkspaceStore& workspaceStore, const WorkspaceLaunchContext& launchContext, QWidget* parent = nullptr);

private:
    void rebuildList();
    void onSelectionChanged();
    void onRenameClicked();
    void onDeleteClicked();

    WorkspaceStore& m_workspaceStore;
    WorkspaceLaunchContext m_launchContext;

    QListWidget* m_list = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};
