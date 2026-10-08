#pragma once

#include <vector>

#include <QString>
#include <QWidget>

class QResizeEvent;
class QStackedWidget;
class QToolButton;
class UiThemeChangeNotifier;

// Side-panel counterpart of BottomPanelWidget (Architecture.md §14.25, §14.41): a header strip of
// checkable panel-tab buttons next to a QStackedWidget of panel-tab content, starting collapsed, but
// oriented for a *horizontal* splitter (vertical button column, collapse axis = width) instead of
// BottomPanelWidget's horizontal button row / collapse-by-height. Serves both the right panel
// (Tags/Preview/Monitoring) and the left panel (Tree/Search) -- the two differ only in which edge the
// header strip sits on, which is always the outer edge of the application window (so the tab names
// stay put and only the content side expands toward the workspace). Splitter-agnostic -- emits
// signals for the owner (MainWindow) to react to rather than reaching into its parent's layout,
// same posture as BottomPanelWidget.
class SidePanelWidget : public QWidget
{
    Q_OBJECT

public:
    // Which side of the workspace this panel is docked on.
    enum class Edge
    {
        Left,  // header strip on this widget's left edge
        Right, // header strip on this widget's right edge
    };

    // `idPrefix` names this widget "<idPrefix>Widget" and its page stack "<idPrefix>Stack"
    // (Architecture.md §11's UI-Automation-id convention), e.g. "rightPanel" -> rightPanelWidget /
    // rightPanelStack.
    SidePanelWidget(Edge edge, const QString& idPrefix, UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent = nullptr);

    // Registers one panel-tab page whose header button gets `buttonObjectName`. Does not switch to
    // it or expand the panel. Returns the page's index (0-based, in registration order).
    int addPanelTab(const QString& label, QWidget* content, const QString& buttonObjectName);

    bool isExpanded() const noexcept { return m_expanded; }

    // Index of the currently active/selected panel tab, or -1 if none (mirrors isExpanded()) --
    // read back by MainWindow::closeEvent to persist panel state (Architecture.md §14.14).
    int activeTabIndex() const noexcept { return m_activeIndex; }

    // Just the header strip's width -- how wide this widget should be while collapsed.
    int collapsedWidth() const;

    // A fixed reasonable content width (Architecture.md §14.25) plus the header strip -- how wide
    // this widget should be while expanded, unless setPreferredExpandedWidth() set an override
    // (e.g. restoring a previously persisted width).
    int preferredExpandedWidth() const;

    // Overrides preferredExpandedWidth() with a specific width restored from persisted state.
    // Ignores values <= 0 (leaves the computed default in place).
    void setPreferredExpandedWidth(int width);

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
    void resizeEvent(QResizeEvent* event) override;

private:
    void onPanelTabButtonClicked(int index);

    // Re-applies m_headerStrip's UiColors::current()-based stylesheet (Architecture.md §14.50).
    void applyThemeColors();

    QWidget* m_headerStrip = nullptr;
    QStackedWidget* m_stack = nullptr;

    std::vector<QToolButton*> m_buttons;
    std::vector<bool> m_everActivated;

    bool m_expanded = false;
    int m_activeIndex = -1;
    int m_preferredExpandedWidthOverride = 0;

    // Last-seen width of the owning QSplitter (our parent), tracked across resizeEvent calls to
    // tell an actual splitter-handle drag (the splitter's own total width is unchanged -- only how
    // it's divided between children moved) apart from this widget merely growing because the whole
    // window was resized/maximized, which proportionally redistributes space across the splitter's
    // children and can push a collapsed panel's width past collapsedWidth() without anyone dragging
    // anything. -1 ("never observed yet") never matches a real width, so the very first resizeEvent
    // never auto-expands. See resizeEvent() for why this distinction matters.
    int m_lastKnownSplitterWidth = -1;
};
