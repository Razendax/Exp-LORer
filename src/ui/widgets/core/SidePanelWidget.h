#pragma once

#include <QString>

#include "CollapsiblePanelWidget.h"

class UiThemeChangeNotifier;

// Side-panel counterpart of BottomPanelWidget (Architecture.md §14.25, §14.41): a header strip of
// checkable panel-tab buttons next to a QStackedWidget of panel-tab content, starting collapsed, but
// oriented for a *horizontal* splitter (vertical button column, collapse axis = width) instead of
// BottomPanelWidget's horizontal button row / collapse-by-height. Serves both the right panel
// (Tags/Preview/Monitoring) and the left panel (Tree/Search) -- the two differ only in which edge the
// header strip sits on, which is always the outer edge of the application window (so the tab names
// stay put and only the content side expands toward the workspace). Splitter-agnostic -- emits
// signals for the owner (MainWindow) to react to rather than reaching into its parent's layout,
// same posture as BottomPanelWidget. Shared collapse/expand/button plumbing lives in
// CollapsiblePanelWidget.
class SidePanelWidget : public CollapsiblePanelWidget
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

    // Just the header strip's width -- how wide this widget should be while collapsed.
    int collapsedWidth() const
    {
        return collapsedExtent();
    }

    // A fixed reasonable content width (Architecture.md §14.25) plus the header strip -- how wide
    // this widget should be while expanded, unless setPreferredExpandedWidth() set an override
    // (e.g. restoring a previously persisted width).
    int preferredExpandedWidth() const;

    // Overrides preferredExpandedWidth() with a specific width restored from persisted state.
    // Ignores values <= 0 (leaves the computed default in place).
    void setPreferredExpandedWidth(int width);

private:
    int m_preferredExpandedWidthOverride = 0;
};
