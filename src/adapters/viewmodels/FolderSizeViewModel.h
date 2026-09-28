#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "DiskUsageNode.h"

class DiskUsageService;
class TabViewModel;

// One per tab (constructed alongside the tab's other per-tab UI pieces, same lazy-start lifetime
// as TerminalWidget -- Architecture.md §14.39). Tracks the bound tab's current path read-only (via
// TabViewModel::currentPathChanged) and drives DiskUsageService; never calls back into
// TabViewModel except navigateTo (indirectly, via navigationRequested -- the owning widget wires
// that straight to tab->navigateTo, keeping this class itself free of a navigateTo call so its
// only TabViewModel dependency is read-only).
class FolderSizeViewModel : public QObject
{
    Q_OBJECT

public:
    FolderSizeViewModel(DiskUsageService& service, TabViewModel* tab, QObject* parent = nullptr);

    bool isScanning() const noexcept { return m_scanning; }
    const std::filesystem::path& currentPath() const noexcept { return m_currentPath; }

public slots:
    // Recursively scans the tab's current folder, persists, then redisplays it. No-op while a scan
    // is already in flight.
    void scanFolder();

    // Recursively scans the drive containing the tab's current folder, persists, then redisplays
    // the tab's current folder (which the scan may have only indirectly covered). No-op while a
    // scan is already in flight.
    void scanDrive();

    // Cache-only redisplay of the current folder's children (no scan) -- called on construction and
    // whenever the tab navigates, so a folder covered by an earlier scan shows its cached data
    // immediately; an uncovered folder shows the empty/"not scanned" state (dataChanged with an
    // empty list) instead of stale or wrong numbers.
    void refreshForCurrentPath();

    // A pie-chart segment (a subfolder) was clicked -- emits navigationRequested for the owning
    // widget to act on. No-op for a file segment (not navigable).
    void onSegmentClicked(const std::filesystem::path& path);

signals:
    void scanningChanged(bool scanning);

    // topChildren is already sorted/truncated (DiskUsageUseCase::topChildren); totalSize is the sum
    // of ALL of folder's immediate children (Architecture.md §14.39 percentage basis), not just the
    // shown subset.
    void dataChanged(const std::filesystem::path& folder, const std::vector<DiskUsageNode>& topChildren, std::uintmax_t totalSize);

    void scanFailed(const QString& message);

    void navigationRequested(const std::filesystem::path& path);

private:
    void startScan(const std::filesystem::path& scanRoot);
    void onTabCurrentPathChanged(const std::filesystem::path& path);
    void setScanning(bool scanning);

    DiskUsageService& m_service;
    TabViewModel* m_tab = nullptr;

    std::filesystem::path m_currentPath;
    bool m_scanning = false;
};
