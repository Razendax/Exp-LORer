#include "FileBrowserView.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include "FileIconDelegate.h"
#include "FileListModel.h"
#include "FileNameEditDelegate.h"
#include "FileTileDelegate.h"
#include "SearchResultDelegate.h"

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
}

FileBrowserView::FileBrowserView(FileListModel* model, QWidget* parent, DisplayMode displayMode)
    : QWidget(parent)
    , m_model(model)
    , m_displayMode(displayMode)
{
    m_listView = new QListView(this);
    m_listView->setModel(m_model);
    m_nameEditDelegate = new FileNameEditDelegate(this);
    m_tileDelegate = new FileTileDelegate(this);
    m_iconDelegate = new FileIconDelegate(this);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setRootIsDecorated(false);
    m_treeView->header()->setSectionsClickable(true);

    if (m_displayMode == DisplayMode::AdvancedSearchResults)
    {
        m_searchResultDelegate = new SearchResultDelegate(this);
        m_treeView->setItemDelegate(m_searchResultDelegate);
    }
    else
    {
        // Installed once here (rather than per setViewMode() call, like the QListView delegates
        // below) since Details view is the QTreeView's only mode and never swaps delegates.
        m_treeView->setItemDelegate(m_nameEditDelegate);
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

    if (event->key() == Qt::Key_F2 && m_displayMode != DisplayMode::AdvancedSearchResults)
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
    if (m_displayMode == DisplayMode::AdvancedSearchResults)
    {
        m_stack->setCurrentWidget(m_treeView);
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
    m_listView->setItemDelegate(m_iconDelegate);
    m_listView->setUniformItemSizes(true);
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setFlow(QListView::LeftToRight);
    m_listView->setWrapping(true);
    m_listView->setGridSize(QSize());
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

void FileBrowserView::setNameHighlightQuery(const QString& query)
{
    if (m_searchResultDelegate)
    {
        m_searchResultDelegate->setHighlightQuery(query);
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
    if (m_displayMode == DisplayMode::AdvancedSearchResults)
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
