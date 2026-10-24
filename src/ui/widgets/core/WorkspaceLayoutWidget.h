#pragma once

#include <array>
#include <optional>
#include <vector>

#include <QWidget>

#include "FileListModel.h"
#include "SplitLayout.h"
#include "WorkspacePaneId.h"

class QVBoxLayout;
class WorkspaceController;
class WorkspacePaneWidget;
class ColumnVisibilitySettingsViewModel;
class DiskUsageService;
class FragmentationService;
class NotificationCenter;
class UiThemeChangeNotifier;
class HotkeyManager;

// Owns all 4 WorkspacePaneWidget instances up front, created once, never destroyed.
// applyLayout(SplitLayout) tears down and rebuilds nested QSplitters from
// WorkspaceLayoutTopology::visiblePanes() to reflect the active SplitLayout. Hidden panes are
// hide()-d, not destroyed, so their tab/navigation state survives a layout switch. Newly-revealed
// empty panes auto-seed one tab at the focused pane's current path. Also forwards
// QApplication::focusChanged up the ancestor chain to WorkspaceController::setFocusedPane,
// keeping WorkspaceController itself free of QWidget dependencies (Architecture.md §14).
class WorkspaceLayoutWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WorkspaceLayoutWidget(WorkspaceController* controller,
                                    ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel,
                                    DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                                    NotificationCenter& notificationCenter, UiThemeChangeNotifier& uiThemeChangeNotifier,
                                    HotkeyManager& hotkeyManager,
                                    const std::array<int, FileListModel::ColumnCount>& initialColumnWidths = {},
                                    const std::array<int, FileListModel::ColumnCount>& initialColumnOrder = {},
                                    QWidget* parent = nullptr);

    WorkspacePaneWidget* paneWidget(WorkspacePaneId id) const;

    // Display-only override used by Media Viewer Mode (docs/features/07-media-viewer.md §14.76) to
    // make the focused pane occupy the whole workspace area regardless of the active SplitLayout,
    // without touching WorkspaceController's own layout() state. Hidden panes keep their tab/
    // navigation state, same as an ordinary layout switch.
    void forceSinglePane(WorkspacePaneId id);
    void clearForcedSinglePane();

private:
    void applyLayout(SplitLayout layout);
    QWidget* buildContent(SplitLayout layout);
    void onFocusChanged(QWidget* old, QWidget* now);

    // WorkspaceLayoutTopology::visiblePanes(layout), or { *m_forcedSinglePane } when the
    // display-only override is active.
    std::vector<WorkspacePaneId> effectiveVisiblePanes(SplitLayout layout) const;

    // Bluish accent on the focused pane's toolbar (UiColors::current().activePaneAccentBackground), but only
    // when more than one pane is visible -- nothing to distinguish it from in the default
    // single-pane layout, so the accent is skipped there.
    void updatePaneActiveStates();

    WorkspaceController* m_controller = nullptr;
    std::array<WorkspacePaneWidget*, 4> m_paneWidgets{};
    QVBoxLayout* m_layout = nullptr;
    QWidget* m_content = nullptr;
    std::optional<WorkspacePaneId> m_forcedSinglePane;
};
