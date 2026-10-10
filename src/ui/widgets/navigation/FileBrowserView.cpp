#include "FileBrowserView.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QEvent>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "FileIconDelegate.h"
#include "FileListModel.h"
#include "FileNameEditDelegate.h"
#include "FileTileDelegate.h"
#include "MediaDetailsDelegate.h"
#include "MediaExtensions.h"
#include "MediaFilmstripDelegate.h"
#include "MediaGridTileDelegate.h"
#include "MediaPreviewFrames.h"
#include "PercentCellDelegate.h"
#include "SearchResultDelegate.h"
#include "TagsCellDelegate.h"

namespace
{
    // Icon-mode sizing steps, matching Explorer's own steps (Architecture.md §2.3.1).
    QSize iconSizeFor(ViewMode mode)
    {
        switch (mode)
        {
            case ViewMode::ExtraLargeIcons:
                return QSize(256, 256);
            case ViewMode::LargeIcons:
                return QSize(96, 96);
            case ViewMode::MediumIcons:
                return QSize(48, 48);
            case ViewMode::Tiles:
                return QSize(48, 48);
            case ViewMode::SmallIcons:
            case ViewMode::List:
            case ViewMode::Details:
                return QSize(16, 16);
        }
        return QSize(48, 48);
    }

    // Qt's QListView never honours setVerticalScrollMode(ScrollPerItem) in IconMode (used for
    // Tiles/ExtraLargeIcons/LargeIcons/MediumIcons/SmallIcons): its icon-mode layout engine
    // (QIconModeViewBase) always falls back to the generic pixel-based scrollbar setup, ignoring
    // the ScrollPerItem property entirely (unlike ListMode, which does honour it). That leaves the
    // wheel scrolling an arbitrary handful of pixels per notch instead of a full row, so icon-family
    // modes get their own wheel handling below, stepping the vertical scrollbar by exactly one row.
    bool isIconFamilyViewMode(ViewMode mode)
    {
        switch (mode)
        {
            case ViewMode::ExtraLargeIcons:
            case ViewMode::LargeIcons:
            case ViewMode::MediumIcons:
            case ViewMode::SmallIcons:
            case ViewMode::Tiles:
                return true;
            case ViewMode::List:
            case ViewMode::Details:
                return false;
        }
        return false;
    }
}

FileBrowserView::FileBrowserView(FileListModel* model, QWidget* parent, DisplayMode displayMode)
    : QWidget(parent)
    , m_model(model)
    , m_displayMode(displayMode)
{
    // Default id; overridden by the owner for a specific role (e.g. SearchPanelWidget renames its
    // results instance to "searchListResultsView") -- see Architecture.md §11's UI Automation convention.
    setObjectName(QStringLiteral("fileBrowserView"));

    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("fileBrowserListView"));
    m_listView->setModel(m_model);

    // Qt's QListView defaults to ScrollPerPixel in IconMode, with a small fixed wheel step
    // unrelated to icon size -- a wheel notch barely moves a 96px/256px icon. ScrollPerItem makes
    // each step one row of items, matching Explorer's own icon-view scrolling.
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    m_nameEditDelegate = new FileNameEditDelegate(this);
    m_tileDelegate = new FileTileDelegate(this);
    m_iconDelegate = new FileIconDelegate(this);
    m_mediaGridDelegate = new MediaGridTileDelegate(this);
    m_mediaFilmstripDelegate = new MediaFilmstripDelegate(this);
    m_mediaDetailsDelegate = new MediaDetailsDelegate(this);
    m_tagsCellDelegate = new TagsCellDelegate(this);
    m_percentCellDelegate = new PercentCellDelegate(this);

    m_treeView = new QTreeView(this);
    m_treeView->setObjectName(QStringLiteral("fileBrowserTreeView"));
    m_treeView->setModel(m_model);
    m_treeView->setRootIsDecorated(false);
    m_treeView->header()->setSectionsClickable(true);
    m_treeView->header()->setSectionsMovable(true);

    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView->header(), &QWidget::customContextMenuRequested, this, &FileBrowserView::showColumnMenu);

    if (m_displayMode == DisplayMode::AdvancedSearchResults)
    {
        m_searchResultDelegate = new SearchResultDelegate(this);
        m_treeView->setItemDelegate(m_searchResultDelegate);
    }
    else if (m_displayMode == DisplayMode::MediaViewerGrid)
    {
        // One-time setup (model/mouse-tracking) for Media Viewer mode (Architecture.md §14.61);
        // the actual per-view-mode rendering (delegate/flow/wrapping/gridSize/iconSize) is
        // delegated to setMediaViewerViewMode() below, called once at the end of this constructor
        // (Architecture.md §14.69), rather than inlined here.

        // Hover-scrub previews (Architecture.md §14.64): mouse-move events only arrive without a
        // button held down when tracking is enabled.
        m_listView->viewport()->setMouseTracking(true);

        // Details view's tag-pill click (Architecture.md §14.72): m_mediaDetailsDelegate is
        // constructed for every FileBrowserView instance regardless of DisplayMode, but is only
        // ever installed as the active delegate -- hence only ever fires editorEvent -- while
        // DisplayMode::MediaViewerGrid + MediaViewerViewMode::Details is active, so this
        // connection is harmless for the other two instances.
        connect(m_mediaDetailsDelegate, &MediaDetailsDelegate::tagActivated, this,
                [this](const QModelIndex& index, Tag::Id tagId, Qt::KeyboardModifiers modifiers) {
                    if (const auto entry = m_model->entryAt(index.row()))
                    {
                        emit mediaViewerDetailsTagClicked(*entry, tagId, modifiers.testFlag(Qt::ControlModifier));
                    }
                });
    }
    else
    {
        // Installed once here (rather than per setViewMode() call, like the QListView delegates
        // below) since Details view is the QTreeView's only mode and never swaps delegates.
        m_treeView->setItemDelegate(m_nameEditDelegate);

        // Per-column override (Architecture.md §14.x): takes precedence over the whole-view
        // delegate above for TagsColumn only, painting colored tag pills instead of plain text.
        m_treeView->setItemDelegateForColumn(FileListModel::TagsColumn, m_tagsCellDelegate);

        // Per-column override for PercentColumn (Architecture.md §14.39): paints the blue fill bar
        // + centered "NN%" label instead of plain text.
        m_treeView->setItemDelegateForColumn(FileListModel::PercentColumn, m_percentCellDelegate);
    }

    // QHeaderView defaults its sort indicator to (column 0, DescendingOrder); QTreeView's
    // setSortingEnabled(true) immediately force-resorts using whatever the header's *current*
    // indicator is before wiring up header-click-to-sort. So the indicator must be synced to the
    // model's actual sort state (which may already be non-default, e.g. a restored session,
    // Architecture.md §14.14) *before* enabling sorting -- otherwise that forced initial sort
    // silently overwrites an already-correct model state with the header's stale default.
    m_treeView->header()->setSortIndicator(FileListModel::columnForCriterion(m_model->sortCriterion()),
                                            m_model->sortAscending() ? Qt::AscendingOrder : Qt::DescendingOrder);
    m_treeView->setSortingEnabled(true);

    // Shared between both views (rather than each QAbstractItemView's own default selection
    // model) so switching ViewMode doesn't drop the current selection.
    m_selectionModel = new QItemSelectionModel(m_model, this);
    m_listView->setSelectionModel(m_selectionModel);
    m_treeView->setSelectionModel(m_selectionModel);

    // ExtendedSelection gives Ctrl+click toggle, Shift+click range-select, and rubber-band
    // drag-select from empty space for free (Architecture.md §14.9). SelectRows ensures a
    // click/drag anywhere in a QTreeView row (Details view has multiple columns) selects the
    // whole row, and selectedRows() reliably enumerates the full selection regardless of column.
    for (QAbstractItemView* view : {static_cast<QAbstractItemView*>(m_listView), static_cast<QAbstractItemView*>(m_treeView)})
    {
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);

        // Qt's default EditTriggers include DoubleClicked, which would race the activated signal
        // (connected below to open/navigate into the item) and pop up the rename editor on every
        // double-click. Explorer only starts a rename via F2, the context menu, or a click on an
        // already-selected item's name -- never a double-click -- so drop DoubleClicked here and
        // rely on beginRename() (F2/context menu/new folder) plus SelectedClicked for that instead.
        view->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    }

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_listView);
    m_stack->addWidget(m_treeView);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(m_listView, &QAbstractItemView::activated, this, &FileBrowserView::emitActivated);
    connect(m_treeView, &QAbstractItemView::activated, this, &FileBrowserView::emitActivated);
    connect(m_selectionModel, &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&) { emitSelectionChanged(); });
    connect(m_model, &FileListModel::sortOrderChanged, this, [this](SortCriterion criterion, bool ascending) {
        m_treeView->header()->setSortIndicator(FileListModel::columnForCriterion(criterion),
                                                ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    });

    m_listView->installEventFilter(this);
    m_treeView->installEventFilter(this);
    // Mouse events for item views arrive on the viewport, not the view itself.
    m_listView->viewport()->installEventFilter(this);
    m_treeView->viewport()->installEventFilter(this);

    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& localPos) { handleContextMenuRequested(m_listView, localPos); });
    connect(m_treeView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& localPos) { handleContextMenuRequested(m_treeView, localPos); });

    for (FileNameEditDelegate* delegate : {static_cast<FileNameEditDelegate*>(m_nameEditDelegate),
                                            static_cast<FileNameEditDelegate*>(m_iconDelegate),
                                            static_cast<FileNameEditDelegate*>(m_tileDelegate)})
    {
        connect(delegate, &FileNameEditDelegate::renameCommitted, this, &FileBrowserView::onRenameCommitted);
    }

    setViewMode(ViewMode::Details);

    if (m_displayMode == DisplayMode::MediaViewerGrid)
    {
        setMediaViewerViewMode(m_mediaViewerViewMode);
    }
}

bool FileBrowserView::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_listView || watched == m_treeView) && event->type() == QEvent::KeyPress)
    {
        if (handleKeyPress(static_cast<QKeyEvent*>(event)))
        {
            return true;
        }
    }

    // See isIconFamilyViewMode()'s comment: Qt's IconMode ignores ScrollPerItem, so icon-family
    // modes get manual wheel handling here, stepping by exactly one row's height per notch rather
    // than Qt's small fixed per-pixel default. Media Viewer's Thumbnails sub-mode (Architecture.md
    // §14.69) is the same IconMode case under a separate m_mediaViewerViewMode flag rather than
    // m_viewMode, so it needs the same treatment explicitly; Filmstrip/Details use ListMode, which
    // does honour ScrollPerItem, so they're left out.
    const bool mediaViewerThumbnailsActive =
        m_displayMode == DisplayMode::MediaViewerGrid && m_mediaViewerViewMode == MediaViewerViewMode::Thumbnails;
    if (watched == m_listView->viewport() && event->type() == QEvent::Wheel
        && (isIconFamilyViewMode(m_viewMode) || mediaViewerThumbnailsActive))
    {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        const int notches = wheelEvent->angleDelta().y() / 120;
        if (notches == 0)
        {
            return QWidget::eventFilter(watched, event);
        }

        const QModelIndex firstIndex = m_model->index(0, 0);
        const int rowStep = firstIndex.isValid() ? m_listView->sizeHintForIndex(firstIndex).height() : 0;
        if (rowStep <= 0)
        {
            return QWidget::eventFilter(watched, event);
        }

        QScrollBar* verticalScrollBar = m_listView->verticalScrollBar();
        verticalScrollBar->setValue(verticalScrollBar->value() - notches * rowStep);
        return true;
    }

    // Hover-scrub previews (Architecture.md §14.64), MediaViewerGrid only: observes MouseMove/Leave
    // without consuming them, so normal item-view hover/drag handling still runs afterward.
    if (watched == m_listView->viewport() && m_displayMode == DisplayMode::MediaViewerGrid
        && (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave))
    {
        const bool isLeave = event->type() == QEvent::Leave;
        const QPoint pos = isLeave ? QPoint() : static_cast<QMouseEvent*>(event)->position().toPoint();
        updateMediaViewerHover(pos, isLeave);
        return QWidget::eventFilter(watched, event);
    }

    // Clicking empty space in the view (no item under the cursor) deselects the current item,
    // matching Explorer. Selection is only cleared, not consumed, so rubber-band selection from
    // empty space still works normally.
    if ((watched == m_listView->viewport() || watched == m_treeView->viewport())
        && event->type() == QEvent::MouseButtonPress)
    {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::BackButton)
        {
            emit navigateBackRequested();
            return true;
        }
        if (mouseEvent->button() == Qt::ForwardButton)
        {
            emit navigateForwardRequested();
            return true;
        }

        auto* view = (watched == m_listView->viewport()) ? static_cast<QAbstractItemView*>(m_listView)
                                                           : static_cast<QAbstractItemView*>(m_treeView);
        // A Ctrl+drag/Shift+drag rubber-band from empty space is meant to *add* to the existing
        // selection, so it must not be wiped here before Qt's own rubber-band handling runs. A
        // plain (no-modifier) empty-space press still clears first to start a fresh selection.
        if (!view->indexAt(mouseEvent->position().toPoint()).isValid()
            && !mouseEvent->modifiers().testFlag(Qt::ControlModifier)
            && !mouseEvent->modifiers().testFlag(Qt::ShiftModifier))
        {
            m_selectionModel->clear();
        }
    }

    return QWidget::eventFilter(watched, event);
}

bool FileBrowserView::handleKeyPress(QKeyEvent* event)
{
    // Must be checked before QKeySequence::Cut: Qt's standard Cut sequence on Windows includes
    // "Shift+Del" as an alternate binding, which file managers repurpose for permanent delete.
    if (event->key() == Qt::Key_Delete)
    {
        emit deleteRequested(event->modifiers().testFlag(Qt::ShiftModifier));
        return true;
    }
    if (event->matches(QKeySequence::Copy))  { emit copyRequested();  return true; }
    if (event->matches(QKeySequence::Cut))   { emit cutRequested();   return true; }
    if (event->matches(QKeySequence::Paste)) { emit pasteRequested(); return true; }
    if (event->key() == Qt::Key_Backspace)   { emit navigateUpRequested(); return true; }

    if (event->key() == Qt::Key_F2 && m_displayMode == DisplayMode::Normal)
    {
        const QModelIndexList selectedRows = m_selectionModel->selectedRows();
        if (selectedRows.size() == 1 && (selectedRows.front().flags() & Qt::ItemIsEditable))
        {
            auto* view = (m_stack->currentWidget() == m_treeView) ? static_cast<QAbstractItemView*>(m_treeView)
                                                                    : static_cast<QAbstractItemView*>(m_listView);
            view->edit(selectedRows.front());
            return true;
        }
    }

    const bool isDetailsView = (m_stack->currentWidget() == m_treeView);
    if (isDetailsView && event->key() == Qt::Key_Left)
    {
        // Navigation below runs synchronously (direct-connected through TabViewModel), so by the
        // time emit() returns m_model already reflects the parent directory if navigation
        // succeeded. Select the folder we just left so it's visible where we came from.
        const std::filesystem::path childPath = m_model->currentDirectory();
        emit navigateUpRequested();
        if (m_model->currentDirectory() != childPath)
        {
            selectEntryByPath(childPath);
        }
        return true;
    }
    if (isDetailsView && event->key() == Qt::Key_Right)
    {
        const QModelIndex current = m_selectionModel->currentIndex();
        if (current.isValid() && current.data(FileListModel::IsDirectoryRole).toBool())
        {
            const std::filesystem::path previousDirectory = m_model->currentDirectory();
            emitActivated(current);
            if (m_model->currentDirectory() != previousDirectory)
            {
                selectFirstEntry();
            }
        }
        return true; // consumed either way - no-op on a file
    }

    return false; // unhandled: falls through to Qt's default keyboardSearch() etc.
}

void FileBrowserView::setViewMode(ViewMode mode)
{
    m_viewMode = mode;

    if (m_displayMode == DisplayMode::AdvancedSearchResults)
    {
        m_stack->setCurrentWidget(m_treeView);
        return;
    }

    if (m_displayMode == DisplayMode::MediaViewerGrid)
    {
        // Rendering is fully set up once at construction time -- mode is ignored here, same
        // "ignore the parameter, lock the rendering" shape as the AdvancedSearchResults branch
        // above.
        m_stack->setCurrentWidget(m_listView);
        return;
    }

    if (mode == ViewMode::Details)
    {
        m_stack->setCurrentWidget(m_treeView);
        return;
    }

    m_stack->setCurrentWidget(m_listView);
    m_listView->setIconSize(iconSizeFor(mode));
    m_listView->setResizeMode(QListView::Adjust);

    if (mode == ViewMode::Tiles)
    {
        m_listView->setItemDelegate(m_tileDelegate);
        m_listView->setViewMode(QListView::IconMode);
        m_listView->setFlow(QListView::LeftToRight);
        m_listView->setWrapping(true);
        m_listView->setUniformItemSizes(true);
        m_listView->setGridSize(QSize(220, 56));
        return;
    }

    if (mode == ViewMode::List)
    {
        m_listView->setItemDelegate(m_nameEditDelegate);
        m_listView->setUniformItemSizes(true);
        m_listView->setViewMode(QListView::ListMode);
        m_listView->setFlow(QListView::TopToBottom);
        m_listView->setWrapping(true);
        m_listView->setGridSize(QSize());
        return;
    }

    // ExtraLargeIcons / LargeIcons / MediumIcons / SmallIcons
    m_iconDelegate->setViewMode(mode);
    m_listView->setItemDelegate(m_iconDelegate);
    m_listView->setUniformItemSizes(true);
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setFlow(QListView::LeftToRight);
    m_listView->setWrapping(true);
    m_listView->setGridSize(QSize());
}

void FileBrowserView::setMediaViewerViewMode(MediaViewerViewMode mode)
{
    if (m_displayMode != DisplayMode::MediaViewerGrid)
    {
        return;
    }

    m_mediaViewerViewMode = mode;
    m_listView->setUniformItemSizes(true);
    m_listView->setGridSize(QSize());

    if (mode == MediaViewerViewMode::Thumbnails)
    {
        // Locked icon-grid rendering (Architecture.md §14.61/§14.62): a real decoded thumbnail +
        // corner metadata chips instead of just the generic file-type icon.
        m_listView->setItemDelegate(m_mediaGridDelegate);
        m_listView->setViewMode(QListView::IconMode);
        m_listView->setFlow(QListView::LeftToRight);
        m_listView->setWrapping(true);
        m_listView->setIconSize(iconSizeFor(ViewMode::ExtraLargeIcons));
        return;
    }

    // Filmstrip/Details (Architecture.md §14.69): single-column rows stretching to the full
    // viewport width, the same way a plain QListWidget behaves -- no gridSize/resize tracking
    // needed.
    m_listView->setItemDelegate(mode == MediaViewerViewMode::Filmstrip
                                     ? static_cast<QAbstractItemDelegate*>(m_mediaFilmstripDelegate)
                                     : static_cast<QAbstractItemDelegate*>(m_mediaDetailsDelegate));
    m_listView->setViewMode(QListView::ListMode);
    m_listView->setFlow(QListView::TopToBottom);
    m_listView->setWrapping(false);
    // Details' thumbnail is sized off this (MediaDetailsDelegate::thumbnailRectForCellRect) --
    // match Thumbnails' own preview size so it isn't a much smaller row-left icon. Filmstrip's
    // band height is fixed in MediaFilmstripDelegate regardless of iconSize, so this only matters
    // for Details.
    m_listView->setIconSize(mode == MediaViewerViewMode::Details ? iconSizeFor(ViewMode::ExtraLargeIcons)
                                                                   : iconSizeFor(ViewMode::LargeIcons));
}

std::array<int, FileListModel::ColumnCount> FileBrowserView::columnWidths() const
{
    std::array<int, FileListModel::ColumnCount> widths{};
    for (int column = 0; column < FileListModel::ColumnCount; ++column)
    {
        widths[static_cast<size_t>(column)] = m_treeView->header()->sectionSize(column);
    }
    return widths;
}

void FileBrowserView::setColumnWidths(const std::array<int, FileListModel::ColumnCount>& widths)
{
    for (int column = 0; column < FileListModel::ColumnCount; ++column)
    {
        const int width = widths[static_cast<size_t>(column)];
        if (width > 0)
        {
            m_treeView->header()->resizeSection(column, width);
        }
    }
}

std::array<int, FileListModel::ColumnCount> FileBrowserView::columnOrder() const
{
    std::array<int, FileListModel::ColumnCount> order{};
    QHeaderView* header = m_treeView->header();
    for (int visualPos = 0; visualPos < FileListModel::ColumnCount; ++visualPos)
    {
        order[static_cast<size_t>(visualPos)] = header->logicalIndex(visualPos);
    }
    return order;
}

void FileBrowserView::setColumnOrder(const std::array<int, FileListModel::ColumnCount>& order)
{
    std::array<bool, FileListModel::ColumnCount> seen{};
    for (int logicalColumn : order)
    {
        if (logicalColumn < 0 || logicalColumn >= FileListModel::ColumnCount || seen[static_cast<size_t>(logicalColumn)])
        {
            return; // not a valid permutation (e.g. the zero-filled "unset" sentinel) -- leave the default order alone
        }
        seen[static_cast<size_t>(logicalColumn)] = true;
    }

    QHeaderView* header = m_treeView->header();
    for (int visualPos = 0; visualPos < FileListModel::ColumnCount; ++visualPos)
    {
        const int logicalColumn = order[static_cast<size_t>(visualPos)];
        const int currentVisualPos = header->visualIndex(logicalColumn);
        if (currentVisualPos != visualPos)
        {
            header->moveSection(currentVisualPos, visualPos);
        }
    }
}

void FileBrowserView::setNameHighlightQuery(const QString& query, bool caseSensitive)
{
    if (m_searchResultDelegate)
    {
        m_searchResultDelegate->setHighlightQuery(query, caseSensitive);
        m_treeView->viewport()->update();
    }
}

void FileBrowserView::emitActivated(const QModelIndex& index)
{
    if (!index.isValid())
    {
        return;
    }

    const auto path = std::filesystem::path(index.data(FileListModel::FilePathRole).toString().toStdWString());
    const bool isDirectory = index.data(FileListModel::IsDirectoryRole).toBool();
    emit itemActivated(path, isDirectory);
}

void FileBrowserView::emitSelectionChanged()
{
    std::vector<FileNode> entries;
    for (const QModelIndex& index : m_selectionModel->selectedRows())
    {
        if (const auto entry = m_model->entryAt(index.row()))
        {
            entries.push_back(*entry);
        }
    }
    emit selectionChanged(entries);
}

std::optional<QModelIndex> FileBrowserView::indexForPath(const std::filesystem::path& path) const
{
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        const auto entry = m_model->entryAt(row);
        if (entry && entry->path() == path)
        {
            return m_model->index(row, 0);
        }
    }
    return std::nullopt;
}

void FileBrowserView::selectEntryByPath(const std::filesystem::path& path)
{
    if (const auto index = indexForPath(path))
    {
        m_selectionModel->setCurrentIndex(*index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_treeView->scrollTo(*index);
        return;
    }

    selectFirstEntry();
}

void FileBrowserView::beginRename(const std::filesystem::path& path)
{
    if (m_displayMode != DisplayMode::Normal)
    {
        return;
    }

    const auto index = indexForPath(path);
    if (!index)
    {
        return;
    }

    m_selectionModel->setCurrentIndex(*index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    auto* view = (m_stack->currentWidget() == m_treeView) ? static_cast<QAbstractItemView*>(m_treeView)
                                                            : static_cast<QAbstractItemView*>(m_listView);
    view->scrollTo(*index);
    view->edit(*index);
}

bool FileBrowserView::selectEntriesContaining(const QString& text)
{
    if (text.isEmpty())
    {
        return false;
    }

    QItemSelection selection;
    QModelIndex firstMatch;
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        const QModelIndex nameIndex = m_model->index(row, FileListModel::NameColumn);
        const QString name = nameIndex.data(Qt::DisplayRole).toString();
        if (name.contains(text, Qt::CaseInsensitive))
        {
            selection.select(nameIndex, nameIndex);
            if (!firstMatch.isValid())
            {
                firstMatch = nameIndex;
            }
        }
    }

    m_selectionModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    if (!firstMatch.isValid())
    {
        return false;
    }

    m_selectionModel->setCurrentIndex(firstMatch, QItemSelectionModel::NoUpdate);
    auto* view = (m_stack->currentWidget() == m_treeView) ? static_cast<QAbstractItemView*>(m_treeView)
                                                            : static_cast<QAbstractItemView*>(m_listView);
    view->scrollTo(firstMatch);
    return true;
}

void FileBrowserView::focusView()
{
    QWidget* view = (m_stack->currentWidget() == m_treeView) ? static_cast<QWidget*>(m_treeView) : static_cast<QWidget*>(m_listView);
    view->setFocus();
}

void FileBrowserView::onRenameCommitted(const QModelIndex& index, const QString& newName)
{
    const auto oldPath = std::filesystem::path(index.data(FileListModel::FilePathRole).toString().toStdWString());
    const std::filesystem::path destination = oldPath.parent_path() / std::filesystem::path(newName.toStdWString());
    emit renameRequested(oldPath, destination);
}

void FileBrowserView::selectFirstEntry()
{
    if (m_model->rowCount() == 0)
    {
        return;
    }

    const QModelIndex index = m_model->index(0, 0);
    m_selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_treeView->scrollTo(index);
}

bool FileBrowserView::isColumnVisible(FileListModel::Column column) const
{
    return !m_treeView->header()->isSectionHidden(column);
}

void FileBrowserView::setColumnVisible(FileListModel::Column column, bool visible)
{
    if (column == FileListModel::NameColumn || isColumnVisible(column) == visible)
    {
        return;
    }

    m_treeView->header()->setSectionHidden(column, !visible);
    emit columnVisibilityChanged(column, visible);
}

void FileBrowserView::showColumnMenu(const QPoint& localPos)
{
    QMenu menu(this);
    for (int column = FileListModel::NameColumn + 1; column < FileListModel::ColumnCount; ++column)
    {
        const auto typedColumn = static_cast<FileListModel::Column>(column);
        QAction* action = menu.addAction(m_model->headerData(column, Qt::Horizontal).toString());
        action->setCheckable(true);
        action->setChecked(isColumnVisible(typedColumn));
        connect(action, &QAction::toggled, this, [this, typedColumn](bool checked) { setColumnVisible(typedColumn, checked); });
    }

    menu.exec(m_treeView->header()->mapToGlobal(localPos));
}

void FileBrowserView::updateMediaViewerHover(const QPoint& pos, bool isLeave)
{
    // Filmstrip shows every preview frame at once -- there's no single-frame hover concept to
    // track (Architecture.md §14.69).
    if (m_mediaViewerViewMode == MediaViewerViewMode::Filmstrip)
    {
        return;
    }

    const bool isDetails = m_mediaViewerViewMode == MediaViewerViewMode::Details;

    QModelIndex newHoveredIndex;
    int newHoveredPreviewIndex = -1;

    if (!isLeave)
    {
        const QModelIndex index = m_listView->indexAt(pos);
        if (index.isValid())
        {
            const auto path = std::filesystem::path(index.data(FileListModel::FilePathRole).toString().toStdWString());
            if (MediaExtensions::isVideoExtension(MediaExtensions::lowercaseExtension(path)))
            {
                const QRect cellRect = m_listView->visualRect(index);
                const QRect thumbnailRect = isDetails
                    ? MediaDetailsDelegate::thumbnailRectForCellRect(cellRect, m_listView->iconSize())
                    : MediaGridTileDelegate::thumbnailRectForCellRect(cellRect, m_listView->iconSize());
                if (thumbnailRect.width() > 0)
                {
                    const double fraction =
                        static_cast<double>(pos.x() - thumbnailRect.left()) / static_cast<double>(thumbnailRect.width());
                    const int band =
                        std::clamp(static_cast<int>(fraction * MediaPreviewFrames::kCount), 0, MediaPreviewFrames::kCount - 1);
                    newHoveredIndex = index;
                    newHoveredPreviewIndex = band;
                }
            }
        }
    }

    const QModelIndex previousHoveredIndex = isDetails ? m_mediaDetailsDelegate->hoveredIndex() : m_mediaGridDelegate->hoveredIndex();
    const int previousHoveredPreviewIndex =
        isDetails ? m_mediaDetailsDelegate->hoveredPreviewIndex() : m_mediaGridDelegate->hoveredPreviewIndex();
    if (previousHoveredIndex == newHoveredIndex && previousHoveredPreviewIndex == newHoveredPreviewIndex)
    {
        return;
    }

    if (isDetails)
    {
        m_mediaDetailsDelegate->setHoveredPreview(newHoveredIndex, newHoveredPreviewIndex);
    }
    else
    {
        m_mediaGridDelegate->setHoveredPreview(newHoveredIndex, newHoveredPreviewIndex);
    }

    if (previousHoveredIndex.isValid())
    {
        m_listView->update(previousHoveredIndex);
    }
    if (newHoveredIndex.isValid())
    {
        m_listView->update(newHoveredIndex);
    }
}

void FileBrowserView::handleContextMenuRequested(QAbstractItemView* view, const QPoint& localPos)
{
    const QModelIndex index = view->indexAt(localPos);
    const QPoint globalPos = view->viewport()->mapToGlobal(localPos);

    if (!index.isValid())
    {
        emit folderContextMenuRequested(globalPos);
        return;
    }

    // Right-clicking an item already part of the current multi-selection leaves the selection
    // intact and acts on all of it, matching Explorer; right-clicking an unselected item
    // collapses the selection down to just that one, same as before.
    if (!m_selectionModel->isSelected(index))
    {
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    std::vector<std::filesystem::path> paths;
    for (const QModelIndex& selectedIndex : m_selectionModel->selectedRows())
    {
        paths.push_back(std::filesystem::path(selectedIndex.data(FileListModel::FilePathRole).toString().toStdWString()));
    }
    emit itemContextMenuRequested(paths, globalPos);
}
