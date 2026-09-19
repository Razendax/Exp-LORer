#pragma once

#include <vector>

#include <QString>
#include <QWidget>

class QResizeEvent;
class QStackedWidget;
class QToolButton;

// Side-panel counterpart of BottomPanelWidget (Architecture.md §14.23): a header strip of
// checkable panel-tab buttons over a QStackedWidget of panel-tab content, starting collapsed, but
// oriented for a *horizontal* splitter (vertical button column on the left edge, collapse axis =
// width) instead of BottomPanelWidget's horizontal button row / collapse-by-height. Only "Tags" is
// registered in v1, but addPanelTab() supports more. Splitter-agnostic -- emits signals for the
// owner (MainWindow) to react to rather than reaching into its parent's layout, same posture as
// BottomPanelWidget.
class RightPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RightPanelWidget(QWidget* parent = nullptr);

    // Registers one panel-tab page. Does not switch to it or expand the panel. Returns the page's
    // index (0-based, in registration order).
    int addPanelTab(const QString& label, QWidget* content);

    bool isExpanded() const noexcept { return m_expanded; }

    // Just the header strip's width -- how wide this widget should be while collapsed.
    int collapsedWidth() const;

    // A fixed reasonable content width (not persisted/configurable in v1, Architecture.md §14.23)
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
