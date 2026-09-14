#pragma once

#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "FileNode.h"
#include "NavigationHistory.h"
#include "ViewMode.h"

class FileNavigationUseCase;

// Binds address-bar/navigation-toolbar UI to FileNavigationUseCase and NavigationHistory. A
// precursor to the PaneViewModel described in Architecture.md §14.3 — once multi-tab/split-pane
// support lands, this behavior is absorbed into PaneViewModel rather than kept standalone. View
// mode (Architecture.md §2.3.1, §14.1) is held here for the same reason.
class NavigationViewModel : public QObject
{
    Q_OBJECT

public:
    explicit NavigationViewModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);

    std::filesystem::path currentPath() const;
    ViewMode viewMode() const noexcept { return m_viewMode; }

public slots:
    // Validates the path via FileNavigationUseCase before recording it in history ("safe
    // navigation"). On failure, history and the current path are left unchanged and
    // navigationFailed is emitted instead. On success, emits currentPathChanged and
    // directoryContentsChanged.
    void navigateTo(const std::filesystem::path& path);

    // Navigates to the parent of the current path, going through the same validation as
    // navigateTo(). No-op if there is no current path or it is already a filesystem root.
    void goUp();

    // Move within existing history without recording a new entry. The target directory is
    // re-validated against disk (it may have been deleted/moved while the user browsed
    // elsewhere), so these can now also emit navigationFailed instead of trusting history
    // unconditionally.
    void goBack();
    void goForward();

    void setViewMode(ViewMode mode);

signals:
    void currentPathChanged(const std::filesystem::path& path);
    void directoryContentsChanged(const std::filesystem::path& path, const std::vector<FileNode>& entries);
    void backAvailableChanged(bool available);
    void forwardAvailableChanged(bool available);
    void upAvailableChanged(bool available);
    void navigationFailed(const std::filesystem::path& path, const QString& message);
    void viewModeChanged(ViewMode mode);

private:
    // Single funnel point for every navigation entry point: fetches directory contents exactly
    // once, and on success emits currentPathChanged + directoryContentsChanged, recording history
    // only when recordHistory is true (false for goBack/goForward, which only move the existing
    // history pointer rather than recording a new entry).
    void loadAndApply(const std::filesystem::path& path, bool recordHistory);
    void emitAvailability();

    FileNavigationUseCase& m_fileNavigationUseCase;
    NavigationHistory m_history;
    ViewMode m_viewMode = ViewMode::Details;
};
