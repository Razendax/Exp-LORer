#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <QPoint>
#include <QWidget>

#include "FileNode.h"
#include "ViewMode.h"

class QModelIndex;
class QAbstractItemView;
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

    // Emitted whenever the full selection changes (including becoming empty), resolved via
    // QItemSelectionModel::selectedRows() -> FileListModel::entryAt. Backs the tag panel's
    // "selected item" target (Architecture.md §14.9); WorkspacePaneWidget connects this 1:1 to
    // the owning tab's setSelectedEntries.
    void selectionChanged(const std::vector<FileNode>& entries);

    // Keyboard hotkeys (Architecture.md §14.10), captured via an event filter on m_listView/
    // m_treeView rather than QShortcut/QAction so they're only active while the file list itself
    // has focus (a window-scoped shortcut would also fire while the address bar has focus).
    void copyRequested();
    void cutRequested();
    void pasteRequested();
    void deleteRequested(bool permanent);   // true for Shift+Delete
    void navigateUpRequested();             // Backspace, and ArrowLeft in Details view

    // Mouse side buttons, captured via the same viewport event filter as the empty-space
    // deselect handling below, rather than QAction shortcuts, so they only apply while the mouse
    // is over the file list (mirrors the keyboard hotkeys' focus scoping above).
    void navigateBackRequested();           // Mouse XButton1 (Qt::BackButton)
    void navigateForwardRequested();        // Mouse XButton2 (Qt::ForwardButton)

    // Right-click on a row (resolved via indexAt; the row is selected first, matching Explorer's
    // "right-click an unselected item selects it") vs. on empty space (Architecture.md §14.13).
    void itemContextMenuRequested(const std::vector<std::filesystem::path>& paths, const QPoint& globalPos);
    void folderContextMenuRequested(const QPoint& globalPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void emitActivated(const QModelIndex& index);
    void emitSelectionChanged();
    bool handleKeyPress(QKeyEvent* event);
    void handleContextMenuRequested(QAbstractItemView* view, const QPoint& localPos);

    // Restores a current/selected row after arrow-key navigation in Details view, since
    // navigating clears the previous selection (TabViewModel::loadAndApply resets it) and would
    // otherwise leave the tree view with no current index, breaking further arrow navigation.
    void selectEntryByPath(const std::filesystem::path& path);
    void selectFirstEntry();

    FileListModel* m_model = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListView* m_listView = nullptr;
    QTreeView* m_treeView = nullptr;
    QAbstractItemDelegate* m_defaultDelegate = nullptr;
    FileTileDelegate* m_tileDelegate = nullptr;
    QItemSelectionModel* m_selectionModel = nullptr;
};
