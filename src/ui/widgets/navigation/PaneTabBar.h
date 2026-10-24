#pragma once

#include <optional>

#include <QTabBar>

#include "WorkspacePaneId.h"

class QMimeData;
class QMouseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

// Cross-pane tab move (spec §3, Milestone M3): installed by WorkspacePaneWidget::createTabArea()
// in place of the default tab bar Qt creates for its QTabWidget, so a tab drag that leaves this
// bar's own rect() can be handed off into a custom QDrag carrying this pane's identity -- the
// only way a drop target (another pane's tab strip or content area) can know which pane/tab the
// drag came from (WorkspacePaneViewModel::takeTab/insertTab and WorkspaceController::moveTab are
// the model-level move Milestone M2 already wired end-to-end behind its context-menu
// alternative; this milestone adds the drag gesture on top of the same primitives). Keeps
// QTabBar::setMovable(true)'s in-bar reorder (Milestone M1) working for drags that never leave
// the bar -- only a drag that actually crosses rect() triggers the hand-off below.
class PaneTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit PaneTabBar(WorkspacePaneId paneId, QWidget* parent = nullptr);

    WorkspacePaneId paneId() const noexcept { return m_paneId; }

    // Mime type carrying the DragPayload below -- also decoded directly by
    // WorkspacePaneWidget::dropEvent() (the content-area drop target), so both sides agree on the
    // same encoding without either depending on the other's internals.
    static constexpr const char* kMimeType = "application/x-explorer-tab";

    // The tab count is captured at drag start so a drop target can reject/show-no-valid-drop
    // feedback for a would-be-emptying move (the pane-must-keep-one-tab guard's *visual* half)
    // without needing a live cross-widget query mid-drag -- the actual rejection is still
    // enforced at drop time by WorkspaceController::moveTab's own tabCount() <= 1 check.
    struct DragPayload
    {
        WorkspacePaneId sourcePaneId = WorkspacePaneId::PaneA;
        int sourceTabIndex = -1;
        int sourceTabCountAtDragStart = 0;
    };

    static QMimeData* encode(const DragPayload& payload);
    static std::optional<DragPayload> decode(const QMimeData* mimeData);

signals:
    // A tab was dropped onto this bar, to be inserted at `destIndex` (position-aware: the drop
    // position's x vs. the hovered tab's rect midpoint, or count() past the last tab). Only ever
    // emitted for an accepted drop (different source pane, source pane had more than one tab at
    // drag start); WorkspacePaneWidget forwards this straight into its own identically-shaped
    // tabDropped signal (a signal-to-signal connection) for WorkspaceLayoutWidget to act on.
    void tabDropped(WorkspacePaneId sourcePaneId, int sourceTabIndex, int destIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Index to insert at for a drop landing at `pos`: the hovered tab's index if left of its
    // midpoint, one past it otherwise; past the last tab (tabAt() == -1) inserts at count().
    int insertionIndexForDrop(const QPoint& pos) const;

    // Builds a QDrag carrying `tabIndex` (the tab currently being dragged, read via currentIndex()
    // at hand-off time rather than whatever was under the initial press -- any native in-bar
    // reorder already forwarded to QTabBar::mouseMoveEvent before the hand-off may have moved it)
    // and runs it to completion. Synchronous (QDrag::exec() blocks); returns once the drag is
    // accepted by a drop target or cancelled (released outside any valid target).
    void beginCrossPaneDrag(int tabIndex);

    WorkspacePaneId m_paneId;

    // Set the moment a drag leaves rect() while the left button is held (mouseMoveEvent), cleared
    // once beginCrossPaneDrag()'s QDrag::exec() returns or on the next mousePressEvent. Freezes
    // further forwarding into QTabBar::mouseMoveEvent (which would otherwise keep driving the
    // now-irrelevant native in-bar drag past the bar's own edge) and guards against re-entering
    // the hand-off path.
    bool m_handingOffToDrag = false;
};
