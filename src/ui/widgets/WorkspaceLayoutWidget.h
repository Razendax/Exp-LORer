#pragma once

#include <array>

#include <QWidget>

#include "FileListModel.h"
#include "SplitLayout.h"
#include "WorkspacePaneId.h"

class QVBoxLayout;
class WorkspaceController;
class WorkspacePaneWidget;

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
                                    const std::array<int, FileListModel::ColumnCount>& initialColumnWidths = {},
                                    QWidget* parent = nullptr);

    WorkspacePaneWidget* paneWidget(WorkspacePaneId id) const;

private:
    void applyLayout(SplitLayout layout);
    QWidget* buildContent(SplitLayout layout);
    void onFocusChanged(QWidget* old, QWidget* now);

    WorkspaceController* m_controller = nullptr;
    std::array<WorkspacePaneWidget*, 4> m_paneWidgets{};
    QVBoxLayout* m_layout = nullptr;
    QWidget* m_content = nullptr;
};
