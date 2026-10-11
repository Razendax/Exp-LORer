#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include "FileListModel.h"
#include "FileNode.h"
#include "MediaViewerViewMode.h"
#include "Tag.h"
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
class MediaGridTileDelegate;
class MediaFilmstripDelegate;
class MediaDetailsDelegate;
class FileNameEditDelegate;
class SearchResultDelegate;
class TagsCellDelegate;
class PercentCellDelegate;

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
    // ignored in this mode. MediaViewerGrid (Architecture.md §14.61) similarly always renders as
    // the QListView page locked to an Extra-Large-Icons-style grid, ignoring setViewMode() the
    // same way.
    enum class DisplayMode
    {
        Normal,
        AdvancedSearchResults,
        MediaViewerGrid,
    };

    explicit FileBrowserView(FileListModel* model, QWidget* parent = nullptr, DisplayMode displayMode = DisplayMode::Normal);

    void setViewMode(ViewMode mode);

    // MediaViewerGrid mode only (Architecture.md §14.69): switches m_listView's installed
    // delegate/flow/wrapping/gridSize/iconSize to render `mode`'s layout. No-op outside
    // DisplayMode::MediaViewerGrid.
    void setMediaViewerViewMode(MediaViewerViewMode mode);

    // AdvancedSearchResults mode only: forwarded to the SearchResultDelegate. No-op in Normal mode.
    // caseSensitive (Architecture.md §14.44) defaults false, preserving the Advanced Search pane's
    // existing always-case-insensitive highlighting.
    void setNameHighlightQuery(const QString& query, bool caseSensitive = false);

    // Details-view (QTreeView) column widths, indexed by FileListModel::Column (Architecture.md
    // §14.14). setColumnWidths() ignores any entry <= 0 (the "unset, use QHeaderView's own
    // default" sentinel used by a fresh AppConfig), so it's safe to call unconditionally right
    // after construction.
    std::array<int, FileListModel::ColumnCount> columnWidths() const;
    void setColumnWidths(const std::array<int, FileListModel::ColumnCount>& widths);

    // Details-view (QTreeView) column left-to-right order (Architecture.md §14.14): entry [visual
    // position] = the FileListModel::Column logical index displayed there. setColumnOrder() is a
    // no-op if `order` isn't a valid permutation of 0..ColumnCount-1 (the "unset, use QHeaderView's
    // own default order" sentinel used by a fresh AppConfig -- a zero-filled array), so it's safe
    // to call unconditionally right after construction, same posture as setColumnWidths() above.
    std::array<int, FileListModel::ColumnCount> columnOrder() const;
    void setColumnOrder(const std::array<int, FileListModel::ColumnCount>& order);

    // Per-tab Details-view column visibility (Architecture.md §14.x), toggled via the header's
    // right-click menu. Unlike columnWidths()/setColumnWidths() above, this is deliberately NOT
    // part of any persisted config -- each tab's visibility lives only on its own QHeaderView
    // instance for the tab's lifetime. column == NameColumn is a no-op guard (Name is always
    // visible; the menu itself never offers it).
    bool isColumnVisible(FileListModel::Column column) const;
    void setColumnVisible(FileListModel::Column column, bool visible);

    // Starts inline in-place rename editing on the row for `path` (Architecture.md §14.13.4/
    // §14.13.6). No-op in AdvancedSearchResults mode, or if `path` isn't currently listed.
    void beginRename(const std::filesystem::path& path);

    // MediaViewerGrid mode only (Architecture.md §14.75): the on-screen rect of `index`'s name
    // text under the current MediaViewerViewMode, in m_listView viewport coordinates. Used by
    // MediaViewerGridListView's click-to-rename hit-test, built from the view itself
    // (m_listView->visualRect/iconSize/font -- the per-row Qt::FontRole override is deliberately
    // skipped here, a harmless approximation for hit-testing only; painting is unaffected). Returns
    // an empty QRect outside DisplayMode::MediaViewerGrid or for an invalid index.
    QRect mediaViewerNameRectFor(const QModelIndex& index) const;

    // Quick-select (status bar, Architecture.md §14.27): selects and scrolls to every row whose
    // displayed Name contains `text` (case-insensitive, substring match anywhere in the name).
    // Empty text is a no-op (leaves the existing selection alone). No match clears the selection,
    // so the field's state always reflects reality. Returns whether any match was found.
    bool selectEntriesContaining(const QString& text);

    // Returns keyboard focus to whichever inner view (list or tree) is currently showing, e.g.
    // after the status bar's quick-select field is dismissed via Escape.
    void focusView();

    // Selects and scrolls to the row for `path`, or falls back to selectFirstEntry() if it isn't
    // currently listed (e.g. its parent just changed out from under it). Originally private,
    // used only for Details-view arrow-key navigation; now also driven by
    // WorkspacePaneWidget::navigateAndSelect (Architecture.md §14.35's "select a favorite's file
    // in its parent listing after navigating there").
    void selectEntryByPath(const std::filesystem::path& path);

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

    // Emitted by setColumnVisible (Architecture.md §14.x), so WorkspacePaneWidget can mirror the
    // change onto this tab's other FileBrowserView instance (normal browsing and advanced-search
    // results share the same visibility per tab).
    void columnVisibilityChanged(FileListModel::Column column, bool visible);

    // Media Viewer Details view's per-item tag-pill click (Architecture.md §14.72), relayed from
    // MediaDetailsDelegate::tagActivated once the row index is resolved to a FileNode.
    void mediaViewerDetailsTagClicked(const FileNode& entry, Tag::Id tagId, bool ctrlHeld);

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
    void selectFirstEntry();

    // Linear scan for the row listing `path`, shared by selectEntryByPath and beginRename.
    std::optional<QModelIndex> indexForPath(const std::filesystem::path& path) const;

    void onRenameCommitted(const QModelIndex& index, const QString& newName);

    // Builds and shows the header's right-click column-visibility menu (Architecture.md §14.x):
    // one checkable QAction per column except NameColumn, checked from isColumnVisible(), toggling
    // via setColumnVisible() on click.
    void showColumnMenu(const QPoint& localPos);

    // MediaViewerGrid-only hover-scrub tracking (Architecture.md §14.64): called from eventFilter()
    // for every viewport MouseMove/Leave. Resolves which video tile (if any) the cursor is over and
    // which of its MediaPreviewFrames::kCount bands `pos` falls in, updates the active delegate's
    // (m_mediaGridDelegate for Thumbnails, m_mediaDetailsDelegate for Details -- a no-op while
    // Filmstrip is active, Architecture.md §14.69) hover state on an actual change, and issues a
    // targeted repaint of just the previous and new hovered cells. isLeave forces the hover state
    // to "cleared" regardless of pos.
    void updateMediaViewerHover(const QPoint& pos, bool isLeave);

    FileListModel* m_model = nullptr;
    DisplayMode m_displayMode = DisplayMode::Normal;
    ViewMode m_viewMode = ViewMode::Details;
    MediaViewerViewMode m_mediaViewerViewMode = MediaViewerViewMode::Thumbnails;
    QStackedWidget* m_stack = nullptr;
    QListView* m_listView = nullptr;
    QTreeView* m_treeView = nullptr;
    FileNameEditDelegate* m_nameEditDelegate = nullptr;
    FileTileDelegate* m_tileDelegate = nullptr;
    SearchResultDelegate* m_searchResultDelegate = nullptr;
    FileIconDelegate* m_iconDelegate = nullptr;
    MediaGridTileDelegate* m_mediaGridDelegate = nullptr;
    MediaFilmstripDelegate* m_mediaFilmstripDelegate = nullptr;
    MediaDetailsDelegate* m_mediaDetailsDelegate = nullptr;
    TagsCellDelegate* m_tagsCellDelegate = nullptr;
    PercentCellDelegate* m_percentCellDelegate = nullptr;
    QItemSelectionModel* m_selectionModel = nullptr;
};
