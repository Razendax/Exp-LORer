#include "FileBrowserView.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QCursor>
#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "FileIconDelegate.h"
#include "FileListModel.h"
#include "FileNameEditDelegate.h"
#include "FileTileDelegate.h"
#include "HotkeyCatalogEntry.h"
#include "HotkeyChord.h"
#include "HotkeyManager.h"
#include "MediaDetailsDelegate.h"
#include "MediaExtensions.h"
#include "MediaFilmstripDelegate.h"
#include "MediaGridTileDelegate.h"
#include "MediaHoverPreviewPopup.h"
#include "MediaPreviewFrames.h"
#include "MediaViewerAnimationController.h"
#include "PercentCellDelegate.h"
#include "SearchResultDelegate.h"
#include "TagsCellDelegate.h"

namespace
{
    // Hover preview popup (Architecture.md §14.76): how long the mouse must settle over a
    // previewable row before the popup is requested.
    constexpr int kHoverPreviewDebounceMs = 350;

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

    // Restricts a SelectedClicked-triggered edit() to a click landing on the current Media Viewer
    // view mode's name rect (Architecture.md §14.75), swapped in for m_listView only in
    // DisplayMode::MediaViewerGrid. Normal mode's identical SelectedClicked trigger already proves
    // Qt's own click/double-click disambiguation doesn't race a double-click-to-activate, so this
    // only narrows *where* a plain click is allowed to start an edit, never *when* -- every other
    // trigger (AllEditTriggers, used by beginRename()/F2) passes straight through unrestricted.
    class MediaViewerGridListView : public QListView
    {
    public:
        MediaViewerGridListView(FileBrowserView* owner, QWidget* parent)
            : QListView(parent)
            , m_owner(owner)
        {
        }

    protected:
        bool edit(const QModelIndex& index, EditTrigger trigger, QEvent* event) override
        {
            if (trigger == QAbstractItemView::SelectedClicked && event)
            {
                const auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (!m_owner->mediaViewerNameRectFor(index).contains(mouseEvent->position().toPoint()))
                {
                    return false;
                }
            }
            return QListView::edit(index, trigger, event);
        }

    private:
        FileBrowserView* m_owner = nullptr;
    };
}

FileBrowserView::FileBrowserView(FileListModel* model, QWidget* parent, HotkeyManager& hotkeyManager, DisplayMode displayMode)
    : QWidget(parent)
    , m_model(model)
    , m_hotkeyManager(hotkeyManager)
    , m_displayMode(displayMode)
{
    // Default id; overridden by the owner for a specific role (e.g. SearchPanelWidget renames its
    // results instance to "searchListResultsView") -- see Architecture.md §11's UI Automation convention.
    setObjectName(QStringLiteral("fileBrowserView"));

    // MediaViewerGrid mode installs MediaViewerGridListView (Architecture.md §14.75) instead of a
    // plain QListView, so a click-to-rename only fires when it lands on the name text itself.
    m_listView = (displayMode == DisplayMode::MediaViewerGrid) ? static_cast<QListView*>(new MediaViewerGridListView(this, this))
                                                                 : new QListView(this);
    m_listView->setObjectName(QStringLiteral("fileBrowserListView"));
    m_listView->setModel(m_model);

    // Qt's QListView defaults to ScrollPerPixel in IconMode, with a small fixed wheel step
    // unrelated to icon size -- a wheel notch barely moves a 96px/256px icon. ScrollPerItem makes
    // each step one row of items, matching Explorer's own icon-view scrolling.
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);

    // Static/Animate/Autoplay GIF preview mode (Architecture.md §14.83): owns the real QMovie
    // lifecycles for this grid's rows, shared across the Thumbnails/Details delegate swap. Only
    // ever needed (and only ever installed as an active delegate's GIF path) in MediaViewerGrid
    // mode -- stays null for the other two DisplayModes, same posture as the GIF-aware delegates
    // below being constructed unconditionally but only ever painting for this DisplayMode.
    if (displayMode == DisplayMode::MediaViewerGrid)
    {
        m_mediaViewerAnimationController = new MediaViewerAnimationController(m_listView, this);
    }

    m_nameEditDelegate = new FileNameEditDelegate(this);
    m_tileDelegate = new FileTileDelegate(this);
    m_iconDelegate = new FileIconDelegate(this);
    m_mediaGridDelegate = new MediaGridTileDelegate(m_mediaViewerAnimationController, this);
    m_mediaFilmstripDelegate = new MediaFilmstripDelegate(this);
    m_mediaDetailsDelegate = new MediaDetailsDelegate(m_mediaViewerAnimationController, this);
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

        // Per-column override, same as the `else` branch below: takes precedence over the
        // whole-view SearchResultDelegate above for TagsColumn only, painting colored tag pills
        // instead of falling through to QStyledItemDelegate's plain-text paint (which has nothing
        // to show, since TagsColumn has no DisplayRole -- only TagsRole) (Architecture.md §14.x).
        m_treeView->setItemDelegateForColumn(FileListModel::TagsColumn, m_tagsCellDelegate);
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
    // FileListModel::setFolderSizes() re-sorts via layoutChanged (not a model reset) when sorted by
    // Size/Percent and folder sizes arrive asynchronously, specifically so the current selection
    // survives the reorder -- but the row's new position may have scrolled out of view, so bring it
    // back into view rather than leaving the selected-but-invisible row stranded off-screen.
    connect(m_model, &FileListModel::layoutChanged, this, [this]() {
        const QModelIndex current = m_selectionModel->currentIndex();
        if (current.isValid())
        {
            m_treeView->scrollTo(current);
        }
    });

    m_listView->installEventFilter(this);
    m_treeView->installEventFilter(this);
    // Mouse events for item views arrive on the viewport, not the view itself.
    m_listView->viewport()->installEventFilter(this);
    m_treeView->viewport()->installEventFilter(this);

    // Hover preview popup (Architecture.md §14.76): mouse-move events only arrive without a
    // button held down when tracking is enabled. Re-setting m_listView's is a harmless no-op
    // where the MediaViewerGrid branch above already set it for its own hover-scrub.
    m_listView->viewport()->setMouseTracking(true);
    m_treeView->viewport()->setMouseTracking(true);

    m_hoverPreviewTimer = new QTimer(this);
    m_hoverPreviewTimer->setSingleShot(true);
    m_hoverPreviewTimer->setInterval(kHoverPreviewDebounceMs);
    connect(m_hoverPreviewTimer, &QTimer::timeout, this, [this]() {
        if (!m_hoverPreviewEntry)
        {
            return;
        }

        // GIF hover always animates (Architecture.md §14.79): bypasses the async
        // decode-then-showPreview() pipeline entirely for this one extension, rather than going
        // through mediaHoverPreviewRequested/showHoverPreviewResult like every other extension.
        const std::string extension = MediaExtensions::lowercaseExtension(m_hoverPreviewEntry->path());
        if (MediaExtensions::isAnimatedImageExtension(extension))
        {
            if (!m_hoverPreviewPopup)
            {
                m_hoverPreviewPopup = new MediaHoverPreviewPopup(this);
            }
            m_hoverPreviewPopup->showAnimatedGif(m_hoverPreviewEntry->path(), QCursor::pos());
            return;
        }

        emit mediaHoverPreviewRequested(*m_hoverPreviewEntry);
    });

    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& localPos) { handleContextMenuRequested(m_listView, localPos); });
    connect(m_treeView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& localPos) { handleContextMenuRequested(m_treeView, localPos); });

    // All six delegates connect renameCommitted -> onRenameCommitted unconditionally, same
    // "connected unconditionally, fires only where actually installed" posture already used for
    // MediaDetailsDelegate::tagActivated above -- harmless no-op wiring for whichever three of
    // m_mediaGridDelegate/m_mediaFilmstripDelegate/m_mediaDetailsDelegate aren't the active
    // delegate on a given instance (Architecture.md §14.75).
    for (FileNameEditDelegate* delegate : {static_cast<FileNameEditDelegate*>(m_nameEditDelegate),
                                            static_cast<FileNameEditDelegate*>(m_iconDelegate),
                                            static_cast<FileNameEditDelegate*>(m_tileDelegate),
                                            static_cast<FileNameEditDelegate*>(m_mediaGridDelegate),
                                            static_cast<FileNameEditDelegate*>(m_mediaFilmstripDelegate),
                                            static_cast<FileNameEditDelegate*>(m_mediaDetailsDelegate)})
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

    // Hover preview popup (Architecture.md §14.76), Normal/AdvancedSearchResults modes only --
    // parallel to, but independent from, the MediaViewerGrid hover-scrub block above. Observes
    // without consuming, same posture.
    if ((watched == m_listView->viewport() || watched == m_treeView->viewport()) && m_displayMode != DisplayMode::MediaViewerGrid
        && (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave))
    {
        const bool isLeave = event->type() == QEvent::Leave;
        const QPoint pos = isLeave ? QPoint() : static_cast<QMouseEvent*>(event)->position().toPoint();
        auto* view = (watched == m_listView->viewport()) ? static_cast<QAbstractItemView*>(m_listView)
                                                           : static_cast<QAbstractItemView*>(m_treeView);
        updateHoverPreview(pos, view, isLeave);
        return QWidget::eventFilter(watched, event);
    }

    // Clicking empty space in the view (no item under the cursor) deselects the current item,
    // matching Explorer. Selection is only cleared, not consumed, so rubber-band selection from
    // empty space still works normally.
    if ((watched == m_listView->viewport() || watched == m_treeView->viewport())
        && event->type() == QEvent::MouseButtonPress)
    {
        // Hover preview popup (Architecture.md §14.76): dismissed immediately on any press so it
        // doesn't linger during a drag-select or click.
        clearHoverPreview();

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
    const QKeySequence chord = HotkeyChord::fromKeyEvent(event).sequence();
    const std::optional<HotkeyAction> action = m_hotkeyManager.resolve(HotkeyCatalogEntry::Scope::View, chord);

    // Independent actions: whichever HotkeyAction the physical chord currently resolves to (via
    // rebind or catalog default) always wins, regardless of which physical key triggered it -- none
    // of these seven have any competing Qt-internal default behavior to worry about suppressing.
    if (action == HotkeyAction::Delete)            { emit deleteRequested(false); return true; }
    if (action == HotkeyAction::DeletePermanently) { emit deleteRequested(true);  return true; }
    if (action == HotkeyAction::Copy)  { emit copyRequested();  return true; }
    if (action == HotkeyAction::Cut)   { emit cutRequested();   return true; }
    if (action == HotkeyAction::Paste) { emit pasteRequested(); return true; }
    if (action == HotkeyAction::NavigateToParent) { emit navigateUpRequested();   return true; }
    if (action == HotkeyAction::Properties)       { emit propertiesRequested();   return true; }

    // Select All (Ctrl+A) and Open/navigate-into-selection (Enter) are each also QAbstractItemView's
    // own built-in key handling (select-all, activate), not something this app ever routed through
    // a QShortcut/QAction -- so once SelectAll/OpenOrNavigateInto is rebound or Cleared away from
    // these physical keys, the key itself must still be swallowed here, or Qt's built-in handling
    // underneath would keep firing regardless of the rebind (defeating "Clear makes it unreachable
    // by keyboard", spec §5). `event->matches(QKeySequence::SelectAll)`/the plain-Enter check below
    // identify the *physical* key regardless of what (if anything) currently resolves to it.
    const bool isSelectAllKey = event->matches(QKeySequence::SelectAll);
    const bool isOpenKey =
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier;

    if (action == HotkeyAction::SelectAll || isSelectAllKey)
    {
        if (action == HotkeyAction::SelectAll && m_model->rowCount() > 0)
        {
            const QModelIndex topLeft = m_model->index(0, 0);
            const QModelIndex bottomRight = m_model->index(m_model->rowCount() - 1, 0);
            m_selectionModel->select(QItemSelection(topLeft, bottomRight),
                                      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        return true;
    }

    if (action == HotkeyAction::OpenOrNavigateInto || isOpenKey)
    {
        if (action == HotkeyAction::OpenOrNavigateInto)
        {
            const QModelIndex current = m_selectionModel->currentIndex();
            if (current.isValid())
            {
                emitActivated(current);
            }
        }
        return true;
    }

    // Rename (F2) is the same story as Select All/Open above, EXCEPT in AdvancedSearchResults mode,
    // where -- unchanged from before this feature -- F2 is deliberately left to Qt's own
    // EditKeyPressed trigger rather than this guarded single-selection-editable check.
    const bool isRenameKey = event->key() == Qt::Key_F2 && m_displayMode != DisplayMode::AdvancedSearchResults;
    if (action == HotkeyAction::Rename || isRenameKey)
    {
        if (action == HotkeyAction::Rename)
        {
            const QModelIndexList selectedRows = m_selectionModel->selectedRows();
            if (selectedRows.size() == 1 && (selectedRows.front().flags() & Qt::ItemIsEditable))
            {
                auto* view = (m_stack->currentWidget() == m_treeView) ? static_cast<QAbstractItemView*>(m_treeView)
                                                                        : static_cast<QAbstractItemView*>(m_listView);
                view->edit(selectedRows.front());
            }
        }
        return true;
    }

    // Details-view ArrowLeft/ArrowRight (parent/open-folder navigation) stay fixed -- explicitly
    // OUT of scope for hotkey customization (spec §6), untouched and independent of every chord/
    // action resolution above.
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

    if (mode == MediaViewerViewMode::Filmstrip && m_mediaViewerAnimationController)
    {
        // Filmstrip shows every preview frame at once (Architecture.md §14.69) -- nothing there
        // should keep decoding GIF frames in the background while it's the active delegate.
        // m_mediaViewerPreviewMode itself is left untouched, so switching back to Thumbnails/
        // Details resumes whatever preview mode was already selected.
        m_mediaViewerAnimationController->stopAllForDelegateSwap();
    }
}

void FileBrowserView::setMediaViewerPreviewMode(MediaViewerPreviewMode mode)
{
    if (m_displayMode != DisplayMode::MediaViewerGrid)
    {
        return;
    }

    m_mediaViewerPreviewMode = mode;
    m_mediaViewerAnimationController->setMode(mode);
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

QRect FileBrowserView::mediaViewerNameRectFor(const QModelIndex& index) const
{
    if (m_displayMode != DisplayMode::MediaViewerGrid || !index.isValid())
    {
        return QRect();
    }

    const QRect cellRect = m_listView->visualRect(index);
    const QSize iconSize = m_listView->iconSize();
    const QFontMetrics fontMetrics(m_listView->font());

    switch (m_mediaViewerViewMode)
    {
        case MediaViewerViewMode::Thumbnails:
            return MediaGridTileDelegate::nameRectForCellRect(cellRect, iconSize, fontMetrics);
        case MediaViewerViewMode::Filmstrip:
            return MediaFilmstripDelegate::captionRectForCellRect(cellRect, fontMetrics);
        case MediaViewerViewMode::Details:
            return MediaDetailsDelegate::nameRectForCellRect(cellRect, iconSize, m_listView->font());
    }
    return QRect();
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

    // Static/Animate/Autoplay GIF preview (Architecture.md §14.83): a parallel, independent check
    // to the video hover-scrub band computed above -- a no-op whenever the current preview mode
    // isn't AnimateOnHover (MediaViewerAnimationController::setHoveredIndex is only ever consulted
    // in that mode).
    if (m_mediaViewerAnimationController)
    {
        QModelIndex animatedHoverIndex;
        if (!isLeave)
        {
            const QModelIndex index = m_listView->indexAt(pos);
            if (index.isValid())
            {
                const auto path = std::filesystem::path(index.data(FileListModel::FilePathRole).toString().toStdWString());
                if (MediaExtensions::isAnimatedImageExtension(MediaExtensions::lowercaseExtension(path)))
                {
                    animatedHoverIndex = index;
                }
            }
        }

        const QModelIndex previousAnimatedHoverIndex = m_mediaViewerAnimationController->hoveredIndex();
        if (previousAnimatedHoverIndex != animatedHoverIndex)
        {
            m_mediaViewerAnimationController->setHoveredIndex(animatedHoverIndex);
            if (previousAnimatedHoverIndex.isValid())
            {
                m_listView->update(previousAnimatedHoverIndex);
            }
            if (animatedHoverIndex.isValid())
            {
                m_listView->update(animatedHoverIndex);
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

bool FileBrowserView::hoverPreviewEnabled()
{
    QSettings settings;
    return settings.value(QLatin1String(kHoverPreviewEnabledSettingsKey), true).toBool();
}

void FileBrowserView::updateHoverPreview(const QPoint& pos, QAbstractItemView* view, bool isLeave)
{
    std::optional<FileNode> candidate;
    if (!isLeave)
    {
        const QModelIndex index = view->indexAt(pos);
        if (index.isValid())
        {
            if (const auto entry = m_model->entryAt(index.row()); entry && !entry->isDirectory())
            {
                const std::string extension = MediaExtensions::lowercaseExtension(entry->path());
                if (MediaExtensions::isImageExtension(extension) || MediaExtensions::isVideoExtension(extension))
                {
                    candidate = entry;
                }
            }
        }
    }

    const std::optional<std::filesystem::path> candidatePath =
        candidate ? std::optional<std::filesystem::path>(candidate->path()) : std::nullopt;
    if (candidatePath == m_hoverPreviewTargetPath)
    {
        return;
    }

    // Row changed (including to a non-previewable row, empty space, or Leave): always clear first.
    clearHoverPreview();

    if (candidate && hoverPreviewEnabled())
    {
        m_hoverPreviewTargetPath = candidatePath;
        m_hoverPreviewEntry = candidate;
        m_hoverPreviewTimer->start();
    }
}

void FileBrowserView::clearHoverPreview()
{
    m_hoverPreviewTimer->stop();
    m_hoverPreviewTargetPath.reset();
    m_hoverPreviewEntry.reset();
    if (m_hoverPreviewPopup)
    {
        m_hoverPreviewPopup->hide();
    }
}

void FileBrowserView::showHoverPreviewResult(const std::filesystem::path& path, const QImage& image)
{
    if (image.isNull() || !m_hoverPreviewTargetPath || path != *m_hoverPreviewTargetPath)
    {
        return;
    }

    if (!m_hoverPreviewPopup)
    {
        m_hoverPreviewPopup = new MediaHoverPreviewPopup(this);
    }
    m_hoverPreviewPopup->showPreview(QPixmap::fromImage(image), QCursor::pos());
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
