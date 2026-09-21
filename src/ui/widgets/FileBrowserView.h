#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

#include <QPoint>
#include <QString>
#include <QWidget>

#include "FileListModel.h"
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
class FileIconDelegate;
class FileNameEditDelegate;
class SearchResultDelegate;

// The tab's content widget: a QStackedWidget switching between a QListView (icon/list/tiles view
// modes) and a QTreeView (details view mode), both bound to the same FileListModel
// (Architecture.md §2.3.1).
class FileBrowserView : public QWidget
{
    Q_OBJECT

public:
    // Normal: the usual Icons/List/Tiles/Details switching driven by setViewMode(). Advanced
    // search results (Architecture.md §14.19) always render as the QTreeView page regardless of
    // ViewMode, with SearchResultDelegate instead of the stock delegate — setViewMode() calls are
    // ignored in this mode.
    enum class DisplayMode
    {
        Normal,
        AdvancedSearchResults,
    };

    explicit FileBrowserView(FileListModel* model, QWidget* parent = nullptr, DisplayMode displayMode = DisplayMode::Normal);

    void setViewMode(ViewMode mode);

    // AdvancedSearchResults mode only: forwarded to the SearchResultDelegate. No-op in Normal mode.
    void setNameHighlightQuery(const QString& query);

    // Details-view (QTreeView) column widths, indexed by FileListModel::Column (Architecture.md
    // §14.14). setColumnWidths() ignores any entry <= 0 (the "unset, use QHeaderView's own
    // default" sentinel used by a fresh AppConfig), so it's safe to call unconditionally right
    // after construction.
    std::array<int, FileListModel::ColumnCount> columnWidths() const;
    void setColumnWidths(const std::array<int, FileListModel::ColumnCount>& widths);

    // Starts inline in-place rename editing on the row for `path` (Architecture.md §14.13.4/
    // §14.13.6). No-op in AdvancedSearchResults mode, or if `path` isn't currently listed.
    void beginRename(const std::filesystem::path& path);

    // Quick-select (status bar, Architecture.md §14.27): selects and scrolls to every row whose
    // displayed Name contains `text` (case-insensitive, substring match anywhere in the name).
    // Empty text is a no-op (leaves the existing selection alone). No match clears the selection,
    // so the field's state always reflects reality. Returns whether any match was found.
    bool selectEntriesContaining(const QString& text);

    // Returns keyboard focus to whichever inner view (list or tree) is currently showing, e.g.
    // after the status bar's quick-select field is dismissed via Escape.
    void focusView();

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

    // Emitted once an in-place rename edit (F2, context-menu Rename, or New Folder's follow-up
    // rename) is committed (Enter/focus-loss with a non-empty, changed name). WorkspacePaneWidget
    // forwards this to FileOperationsController::renamePath.
    void renameRequested(const std::filesystem::path& source, const std::filesystem::path& destination);

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

    // Linear scan for the row listing `path`, shared by selectEntryByPath and beginRename.
    std::optional<QModelIndex> indexForPath(const std::filesystem::path& path) const;

    void onRenameCommitted(const QModelIndex& index, const QString& newName);

    FileListModel* m_model = nullptr;
    DisplayMode m_displayMode = DisplayMode::Normal;
    QStackedWidget* m_stack = nullptr;
    QListView* m_listView = nullptr;
    QTreeView* m_treeView = nullptr;
    FileNameEditDelegate* m_nameEditDelegate = nullptr;
    FileTileDelegate* m_tileDelegate = nullptr;
    SearchResultDelegate* m_searchResultDelegate = nullptr;
    FileIconDelegate* m_iconDelegate = nullptr;
    QItemSelectionModel* m_selectionModel = nullptr;
};
