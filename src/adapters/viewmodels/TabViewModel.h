#pragma once

#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "FileNode.h"
#include "NavigationHistory.h"
#include "ViewMode.h"

class FileNavigationUseCase;
class FileListModel;

// Binds address-bar/navigation-toolbar UI to FileNavigationUseCase and NavigationHistory for a
// single tab. Owns the FileListModel that backs that tab's FileBrowserView, wiring
// directoryContentsChanged -> FileListModel::setEntries internally so callers don't have to.
// Per-tab content ViewModel owned by WorkspacePaneViewModel (Architecture.md §14).
class TabViewModel : public QObject
{
    Q_OBJECT

public:
    explicit TabViewModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);

    std::filesystem::path currentPath() const;
    ViewMode viewMode() const noexcept { return m_viewMode; }
    FileListModel* fileListModel() const noexcept { return m_fileListModel; }

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
    FileListModel* m_fileListModel = nullptr;
};
