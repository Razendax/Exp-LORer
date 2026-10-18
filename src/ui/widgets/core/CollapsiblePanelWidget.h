#pragma once

#include <functional>
#include <vector>

#include <QString>
#include <QWidget>

class QResizeEvent;
class QStackedWidget;
class QToolButton;
class UiThemeChangeNotifier;

// Shared header-strip-of-checkable-tab-buttons + collapsible content-stack plumbing behind
// SidePanelWidget (collapses by width, vertical button column) and BottomPanelWidget (collapses
// by height, horizontal button row) -- Architecture.md §14.23/§14.25/§14.41. Subclasses supply the
// header strip's button-layout orientation and which dimension (width/height) collapse acts on;
// this base owns button/page bookkeeping, expand/collapse transitions, and the handle-drag-vs-
// window-resize disambiguation in resizeEvent -- previously hand-rolled, and already drifted,
// independently in both subclasses.
class CollapsiblePanelWidget : public QWidget
{
    Q_OBJECT

public:
    bool isExpanded() const noexcept { return m_expanded; }

    // Index of the currently active/selected panel tab, or -1 if none (mirrors isExpanded()).
    int activeTabIndex() const noexcept { return m_activeIndex; }

    // Shows/hides a panel tab's header button without touching addPanelTabCore/activateTab's own
    // machinery (Architecture.md §14.61) -- lets a tab registered up front start hidden and only
    // appear while relevant.
    void setPanelTabButtonVisible(int index, bool visible);

    // Thin public wrapper for onPanelTabButtonClicked(), so restoring a persisted active tab goes
    // through exactly the same expand/select/lazy-activate logic a real click uses.
    void activateTab(int index);

signals:
    // Fires whenever expansion state flips (collapsed <-> expanded), so the owning QSplitter can
    // resize itself.
    void expansionChanged(bool expanded);

    // Fires exactly once per panel tab, the first time it's ever made the active/expanded page --
    // lets the owner lazily initialize that page's content on first use.
    void panelTabFirstActivated(int index);

protected:
    // Which dimension collapse acts on: Horizontal = this widget's width (SidePanelWidget),
    // Vertical = its height (BottomPanelWidget).
    enum class Axis
    {
        Horizontal,
        Vertical,
    };

    // `headerButtonOrientation` is the header strip's own internal box layout direction (Vertical:
    // a top-aligned button column; Horizontal: a left-aligned button row). `headerBackgroundColor`
    // supplies the strip's UiColors::current()-based background on construction and on every
    // theme change -- a plain callable rather than a virtual, since it's needed before the derived
    // class's own constructor body has run. Builds m_headerStrip/m_stack only; arranging them into
    // this widget's own outer layout (which differs: BottomPanelWidget always stacks header above
    // content, SidePanelWidget's header sits on whichever outer edge Edge selects) is left to the
    // derived constructor.
    CollapsiblePanelWidget(Axis axis, Qt::Orientation headerButtonOrientation, std::function<QString()> headerBackgroundColor,
                           UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent);

    // Registers one panel-tab page whose header button gets `buttonObjectName`. Does not switch to
    // it or expand the panel. Returns the page's index (0-based, in registration order).
    int addPanelTabCore(const QString& label, QWidget* content, const QString& buttonObjectName);

    // Number of panel tabs registered so far -- lets a derived addPanelTab() that needs to know its
    // own about-to-be-assigned index for naming (e.g. BottomPanelWidget's button object names)
    // compute it before calling addPanelTabCore().
    std::size_t panelTabCount() const noexcept
    {
        return m_buttons.size();
    }

    void resizeEvent(QResizeEvent* event) override;

    // Just the header strip's extent along the collapse axis -- how wide/tall (per `axis`) this
    // widget should be while collapsed.
    int collapsedExtent() const;

    QWidget* m_headerStrip = nullptr;
    QStackedWidget* m_stack = nullptr;

private:
    void onPanelTabButtonClicked(int index);
    void applyThemeColors();

    Axis m_axis;
    std::function<QString()> m_headerBackgroundColor;

    std::vector<QToolButton*> m_buttons;
    std::vector<bool> m_everActivated;

    bool m_expanded = false;
    int m_activeIndex = -1;

    // Last-seen extent (width for Axis::Horizontal, height for Axis::Vertical) of the owning
    // QSplitter (our parent), tracked across resizeEvent calls to tell an actual splitter-handle
    // drag (the splitter's own total extent is unchanged -- only how it's divided between children
    // moved) apart from this widget merely growing because the whole window was resized/maximized,
    // which proportionally redistributes space across the splitter's children and can push a
    // collapsed panel's extent past collapsedExtent() without anyone dragging anything. -1 ("never
    // observed yet") never matches a real extent, so the very first resizeEvent never auto-expands.
    // See resizeEvent() for why this distinction matters.
    int m_lastKnownSplitterExtent = -1;
};
