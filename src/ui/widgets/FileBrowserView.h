#pragma once

#include <filesystem>
#include <optional>

#include <QWidget>

#include "FileNode.h"
#include "ViewMode.h"

class QModelIndex;
class QItemSelection;
class QItemSelectionModel;
class QStackedWidget;
class QListView;
class QTreeView;
class QAbstractItemDelegate;
class QKeyEvent;
class FileListModel;
class FileTileDelegate;

// The tab's content widget: a QStackedWidget switching between a QListView (icon/list/tiles view
// modes) and a QTreeView (details view mode), both bound to the same FileListModel
// (Architecture.md §2.3.1).
class FileBrowserView : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserView(FileListModel* model, QWidget* parent = nullptr);

    void setViewMode(ViewMode mode);

signals:
    // Emitted on double-click/Enter on a row; isDirectory decides whether MainWindow forwards
    // this to TabViewModel::navigateTo (file activation is out of scope, no viewer yet).
    void itemActivated(const std::filesystem::path& path, bool isDirectory);

    // Emitted whenever the current row changes (including becoming unselected), resolved via
    // FileListModel::entryAt. Backs the tag panel's "selected item" target (Architecture.md
    // §14.9); WorkspacePaneWidget connects this 1:1 to the owning tab's setSelectedEntry.
    void selectionChanged(const std::optional<FileNode>& entry);

    // Keyboard hotkeys (Architecture.md §14.10), captured via an event filter on m_listView/
    // m_treeView rather than QShortcut/QAction so they're only active while the file list itself
    // has focus (a window-scoped shortcut would also fire while the address bar has focus).
    void copyRequested();
    void cutRequested();
    void pasteRequested();
    void deleteRequested(bool permanent);   // true for Shift+Delete
    void navigateUpRequested();             // Backspace, and ArrowLeft in Details view

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void emitActivated(const QModelIndex& index);
    void emitSelectionChanged(const QModelIndex& current);
    bool handleKeyPress(QKeyEvent* event);

    FileListModel* m_model = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListView* m_listView = nullptr;
    QTreeView* m_treeView = nullptr;
    QAbstractItemDelegate* m_defaultDelegate = nullptr;
    FileTileDelegate* m_tileDelegate = nullptr;
    QItemSelectionModel* m_selectionModel = nullptr;
};
