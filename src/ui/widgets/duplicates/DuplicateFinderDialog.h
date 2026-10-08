#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <QDialog>

#include "DuplicateFinderWorker.h"
#include "Result.h"

class DuplicateFinderService;
class FileNavigationUseCase;
class NotificationCenter;
class DuplicateGroupsModel;
class QCloseEvent;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QComboBox;
class QTreeView;
class QPoint;

// Tools > Find Duplicate Files... (Architecture.md §14.51): a non-modal dialog that recursively
// scans a chosen folder or drive, groups files with byte-identical content (never the existing
// identity hash, which mixes in mtime and is unsuitable here), and lets the user check files to
// send to the Recycle Bin. Deliberately an isolated vertical slice -- it does not touch FileNode,
// FileListModel, tagging, or Preview, reusing only IFileSystemRepository's recursive listing
// (through DuplicateFinderService) and the existing trash-delete path (through
// FileNavigationUseCase). Scan results are never persisted -- every Scan click re-reads the live
// filesystem, since a stale cached group would be a correctness hazard for a delete action.
class DuplicateFinderDialog : public QDialog
{
    Q_OBJECT

public:
    // initialPath: the focused tab's current path at the moment the dialog is opened -- a one-time
    // snapshot, not live-following later navigation (same "fixed at first use" posture as the
    // Terminal's cwd, Architecture.md §14.24).
    DuplicateFinderDialog(DuplicateFinderService& duplicateFinderService, FileNavigationUseCase& fileNavigationUseCase,
                          NotificationCenter& notificationCenter, const std::filesystem::path& initialPath,
                          QWidget* parent = nullptr);
    ~DuplicateFinderDialog() override;

protected:
    // Closing the dialog while a scan is in flight cancels it too -- simpler than a confirm-before-
    // stopping prompt, since cancelling a scan loses nothing.
    void closeEvent(QCloseEvent* event) override;

signals:
    // "Open Containing Folder" on a result row (Architecture.md §14.51) -- MainWindow navigates the
    // focused pane there and selects the file, same convenience as a search-result double-click.
    void openContainingFolderRequested(const std::filesystem::path& path);

private:
    void browseForFolder();
    void scanFolder();
    void scanDrive();
    void startScan(const std::filesystem::path& root);
    void cancelActiveScan();
    void onScanProgress(std::size_t hashed, std::size_t totalCandidates);
    void onScanFinished(Result<std::vector<DuplicateGroupInfo>> result);
    void deleteChecked();
    void showResultsContextMenu(const QPoint& pos);
    void updateButtonsEnabled();
    std::uintmax_t minFileSizeBytes() const;

    DuplicateFinderService& m_duplicateFinderService;
    FileNavigationUseCase& m_fileNavigationUseCase;
    NotificationCenter& m_notificationCenter;

    QLineEdit* m_rootPathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QSpinBox* m_minSizeSpinBox = nullptr;
    QComboBox* m_minSizeUnitCombo = nullptr;
    QPushButton* m_scanFolderButton = nullptr;
    QPushButton* m_scanDriveButton = nullptr;
    QPushButton* m_checkAllButNewestButton = nullptr;
    QPushButton* m_checkAllButOldestButton = nullptr;
    QPushButton* m_deleteCheckedButton = nullptr;
    QTreeView* m_resultsView = nullptr;
    DuplicateGroupsModel* m_resultsModel = nullptr;

    bool m_scanning = false;
    int m_activeRequestId = 0;
};
