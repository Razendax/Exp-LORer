#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "FileFragmentationInfo.h"
#include "FragmentationSummary.h"
#include "NotificationCenter.h"

class FragmentationService;
class TabViewModel;

// One per tab (constructed alongside the tab's other per-tab UI pieces, same lazy-start lifetime as
// FolderSizeViewModel/TerminalWidget -- Architecture.md §14.52). Tracks the bound tab's current path
// read-only (via TabViewModel::currentPathChanged) and holds a NotificationCenter& it uses directly
// to create/update/terminate its own cancellable progress-bar card per scan -- the same "ViewModel
// calls into NotificationCenter directly" posture §14.37 established for TagListViewModel/
// WorkspaceController. Its only TabViewModel dependency is read-only, same posture as
// FolderSizeViewModel.
class FragmentationViewModel : public QObject
{
    Q_OBJECT

public:
    FragmentationViewModel(FragmentationService& service, NotificationCenter& notificationCenter, TabViewModel* tab,
                            QObject* parent = nullptr);

    bool isScanning() const noexcept { return m_scanning; }

public slots:
    // Scans the tab's current folder (or its drive root), persists, then redisplays the tab's
    // current folder. No-op while this tab's own scan is already in flight -- a different tab's
    // concurrent scan doesn't block it.
    void scanFolder(std::uintmax_t minFileSizeBytes);
    void scanDrive(std::uintmax_t minFileSizeBytes);

    // Cache-only redisplay of the current folder's files (no scan) -- called on construction and
    // whenever the tab navigates, so a folder covered by an earlier scan shows its cached data
    // immediately; an unscanned folder shows the empty/"not scanned" state (resultsChanged with an
    // empty list) instead of stale or wrong numbers.
    void refreshForCurrentPath();

    // Re-emits openContainingFolderRequested -- called directly by the owning widget's
    // context-menu action, same "emit a plain path-carrying signal, let the owning widget call
    // back into the pane" posture FolderSizeViewModel::onSegmentClicked/navigationRequested
    // established.
    void requestOpenContainingFolder(const std::filesystem::path& path);

signals:
    void scanningChanged(bool scanning);

    // files/summary are redisplayed alike whether this came from a fresh scan or a cache-only
    // lookup -- an empty list (never scanned) is not an error.
    void resultsChanged(const std::vector<FileFragmentationInfo>& files, const FragmentationSummary& summary);

    void scanFailed(const QString& message);

    void openContainingFolderRequested(const std::filesystem::path& path);

private:
    void startScan(const std::filesystem::path& scanRoot, std::uintmax_t minFileSizeBytes);
    void onTabCurrentPathChanged(const std::filesystem::path& path);
    void setScanning(bool scanning);

    FragmentationService& m_service;
    NotificationCenter& m_notificationCenter;
    TabViewModel* m_tab = nullptr;

    std::filesystem::path m_currentPath;
    bool m_scanning = false;
    NotificationCenter::ProgressId m_activeProgressId = 0;
};
