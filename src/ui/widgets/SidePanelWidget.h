#pragma once

#include <vector>

#include <QString>
#include <QWidget>

class QResizeEvent;
class QStackedWidget;
class QToolButton;

// Side-panel counterpart of BottomPanelWidget (Architecture.md §14.25, §14.41): a header strip of
// checkable panel-tab buttons next to a QStackedWidget of panel-tab content, starting collapsed, but
// oriented for a *horizontal* splitter (vertical button column, collapse axis = width) instead of
// BottomPanelWidget's horizontal button row / collapse-by-height. Serves both the right panel
// (Tags/Preview/Monitoring) and the left panel (Tree) -- the two differ only in which edge the
// header strip sits on, which is always the edge facing the workspace. Splitter-agnostic -- emits
// signals for the owner (MainWindow) to react to rather than reaching into its parent's layout,
// same posture as BottomPanelWidget.
class SidePanelWidget : public QWidget
{
    Q_OBJECT

public:
    // Which side of the workspace this panel is docked on.
    enum class Edge
    {
        Left,  // header strip on this widget's right edge
        Right, // header strip on this widget's left edge
    };

    // `idPrefix` names this widget "<idPrefix>Widget" and its page stack "<idPrefix>Stack"
    // (Architecture.md §11's UI-Automation-id convention), e.g. "rightPanel" -> rightPanelWidget /
    // rightPanelStack.
    SidePanelWidget(Edge edge, const QString& idPrefix, QWidget* parent = nullptr);

    // Registers one panel-tab page whose header button gets `buttonObjectName`. Does not switch to
    // it or expand the panel. Returns the page's index (0-based, in registration order).
    int addPanelTab(const QString& label, QWidget* content, const QString& buttonObjectName);

    bool isExpanded() const noexcept { return m_expanded; }

    // Just the header strip's width -- how wide this widget should be while collapsed.
    int collapsedWidth() const;

    // A fixed reasonable content width (not persisted/configurable in v1, Architecture.md §14.25)
    // plus the header strip -- how wide this widget should be while expanded.
    int preferredExpandedWidth() const;

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

    QWidget* m_headerStrip = nullptr;
    QStackedWidget* m_stack = nullptr;

    std::vector<QToolButton*> m_buttons;
    std::vector<bool> m_everActivated;

    bool m_expanded = false;
    int m_activeIndex = -1;
};
