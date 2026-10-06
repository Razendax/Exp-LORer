#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QWidget>

#include "DiskUsageNode.h"

class DiskUsageService;
class NotificationCenter;
class TabViewModel;
class FolderSizeViewModel;
class PieChartWidget;
class QPushButton;

// The bottom-panel's "Folder Size" tab content (Architecture.md §14.39): a PieChartWidget plus a
// narrow column of "Scan Drive"/"Scan Folder" buttons. Owns one FolderSizeViewModel (constructed
// here, same per-tab lazy-start lifetime as TerminalWidget) wired straight to `tab`; segment clicks
// are wired to tab->navigateTo, the one exception to this feature otherwise never calling back into
// TabViewModel.
class FolderSizeWidget : public QWidget
{
    Q_OBJECT

public:
    FolderSizeWidget(DiskUsageService& service, NotificationCenter& notificationCenter, TabViewModel* tab, QWidget* parent = nullptr);

private:
    void updateButtonsEnabled();
    void onScanningChanged(bool scanning);
    void onDataChanged(const std::filesystem::path& folder, const std::vector<DiskUsageNode>& topChildren,
                        std::uintmax_t totalSize);

    TabViewModel* m_tab = nullptr;
    FolderSizeViewModel* m_viewModel = nullptr;
    PieChartWidget* m_pieChart = nullptr;
    QPushButton* m_scanDriveButton = nullptr;
    QPushButton* m_scanFolderButton = nullptr;
};
