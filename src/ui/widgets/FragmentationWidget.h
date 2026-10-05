#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QWidget>

#include "FileFragmentationInfo.h"
#include "FragmentationSummary.h"

class FragmentationService;
class NotificationCenter;
class TabViewModel;
class FragmentationViewModel;
class FragmentationResultsModel;
class QPushButton;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QTreeView;
class QPoint;

// The bottom-panel's "Fragmentation" tab content (Architecture.md §14.52): a toolbar of Scan
// Folder/Scan Drive buttons plus a minimum-file-size filter and a "Show only fragmented files"
// checkbox, a summary label row, and a flat sortable results table. Owns one
// FragmentationViewModel (constructed here, same per-tab lazy-start lifetime as TerminalWidget/
// FolderSizeWidget) wired straight to `tab`. Read-only by nature -- no rename/tag/preview/delete
// actions, just "Open Containing Folder".
class FragmentationWidget : public QWidget
{
    Q_OBJECT

public:
    FragmentationWidget(FragmentationService& service, NotificationCenter& notificationCenter, TabViewModel* tab,
                         QWidget* parent = nullptr);

signals:
    // "Open Containing Folder" on a result row -- the owning WorkspacePaneWidget (which already
    // owns `this` at the wiring site) connects this straight to its own
    // navigateAndSelect(path.parent_path(), path), same posture
    // FolderSizeViewModel::navigationRequested established.
    void openContainingFolderRequested(const std::filesystem::path& path);

private:
    void updateButtonsEnabled();
    void onScanningChanged(bool scanning);
    void onResultsChanged(const std::vector<FileFragmentationInfo>& files, const FragmentationSummary& summary);
    void showResultsContextMenu(const QPoint& pos);
    std::uintmax_t minFileSizeBytes() const;

    TabViewModel* m_tab = nullptr;
    FragmentationViewModel* m_viewModel = nullptr;
    FragmentationResultsModel* m_resultsModel = nullptr;

    QPushButton* m_scanFolderButton = nullptr;
    QPushButton* m_scanDriveButton = nullptr;
    QSpinBox* m_minSizeSpinBox = nullptr;
    QComboBox* m_minSizeUnitCombo = nullptr;
    QCheckBox* m_showOnlyFragmentedCheckBox = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QTreeView* m_resultsView = nullptr;
};
