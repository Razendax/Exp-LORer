#pragma once

#include <QWidget>

#include "MediaViewerGroupMode.h"
#include "MediaViewerPreviewMode.h"
#include "MediaViewerSortCriterion.h"
#include "MediaViewerViewMode.h"

class QComboBox;

// Media Viewer mode's toolbar sort/view controls (Architecture.md §14.68/§14.74/§14.83): five
// tooltipped, icon/text-free combo boxes (sort-by, sort-direction, view, group, preview) grouped
// in one container widget, added to WorkspacePaneWidget's per-pane QToolBar right after the
// address bar.
// Visible/enabled only while Media Viewer mode is active
// (WorkspacePaneWidget::updateToolBarEnabledForMediaViewer); plain QWidget, same shape as
// AddressBarWidget -- no theme notifier needed.
class MediaViewerSortViewBar : public QWidget
{
    Q_OBJECT

public:
    explicit MediaViewerSortViewBar(QWidget* parent = nullptr);

    // QSignalBlocker-guarded resync, mirrors MediaViewerSearchPanelWidget::setKind's posture --
    // used both on tab bind and whenever the bound tab's own state changes elsewhere.
    void setSortCriterion(MediaViewerSortCriterion criterion);
    void setSortAscending(bool ascending);
    void setViewMode(MediaViewerViewMode mode);
    void setGroupMode(MediaViewerGroupMode mode);
    void setPreviewMode(MediaViewerPreviewMode mode);

signals:
    void sortCriterionChanged(MediaViewerSortCriterion criterion);
    void sortAscendingChanged(bool ascending);
    void viewModeChanged(MediaViewerViewMode mode);
    void groupModeChanged(MediaViewerGroupMode mode);
    void previewModeChanged(MediaViewerPreviewMode mode);

private:
    QComboBox* m_sortCriterionCombo = nullptr;
    QComboBox* m_sortDirectionCombo = nullptr;
    QComboBox* m_viewModeCombo = nullptr;
    QComboBox* m_groupModeCombo = nullptr;
    QComboBox* m_previewModeCombo = nullptr;
};
