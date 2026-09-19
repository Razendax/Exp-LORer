#pragma once

#include <vector>

#include <QString>
#include <QWidget>

class QResizeEvent;
class QStackedWidget;
class QToolButton;

// Generic VS Code-style collapsible bottom panel (Architecture.md §14.23): a header strip of
// checkable panel-tab buttons over a QStackedWidget of panel-tab content, starting collapsed. Only
// "Terminal" is registered in v1, but addPanelTab() supports more. Splitter-agnostic -- emits
// signals for the owner (WorkspacePaneWidget) to react to rather than reaching into its parent's
// layout, same posture as AddressBarWidget emitting folderSuggestionsRequested.
class BottomPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BottomPanelWidget(QWidget* parent = nullptr);

    // Registers one panel-tab page. Does not switch to it or expand the panel. Returns the page's
    // index (0-based, in registration order).
    int addPanelTab(const QString& label, QWidget* content);

    bool isExpanded() const noexcept { return m_expanded; }

    // Just the header strip's height -- how tall this widget should be while collapsed.
    int collapsedHeight() const;

    // A fixed reasonable content height (not persisted/configurable in v1, Architecture.md §14.23)
    // plus the header strip -- how tall this widget should be while expanded.
    int preferredExpandedHeight() const;

signals:
    // Fires whenever expansion state flips (collapsed <-> expanded), so the owning QSplitter can
    // resize itself.
    void expansionChanged(bool expanded);

    // Fires exactly once per panel tab, the first time it's ever made the active/expanded page --
    // lets the owner lazily start that page's backing process (e.g. the terminal's cmd.exe)
    // instead of at tab-creation time.
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
