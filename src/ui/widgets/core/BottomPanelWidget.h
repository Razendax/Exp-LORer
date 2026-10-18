#pragma once

#include <QString>

#include "CollapsiblePanelWidget.h"

class UiThemeChangeNotifier;

// Generic VS Code-style collapsible bottom panel (Architecture.md §14.23): a header strip of
// checkable panel-tab buttons over a QStackedWidget of panel-tab content, starting collapsed. Only
// "Terminal" is registered in v1, but addPanelTab() supports more. Splitter-agnostic -- emits
// signals for the owner (WorkspacePaneWidget) to react to rather than reaching into its parent's
// layout, same posture as AddressBarWidget emitting folderSuggestionsRequested. Shared
// collapse/expand/button plumbing lives in CollapsiblePanelWidget.
class BottomPanelWidget : public CollapsiblePanelWidget
{
    Q_OBJECT

public:
    explicit BottomPanelWidget(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent = nullptr);

    // Registers one panel-tab page. Does not switch to it or expand the panel. Returns the page's
    // index (0-based, in registration order).
    int addPanelTab(const QString& label, QWidget* content);

    // Just the header strip's height -- how tall this widget should be while collapsed.
    int collapsedHeight() const
    {
        return collapsedExtent();
    }

    // A fixed reasonable content height (not persisted/configurable in v1, Architecture.md §14.23)
    // plus the header strip -- how tall this widget should be while expanded.
    int preferredExpandedHeight() const;
};
