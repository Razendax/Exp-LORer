#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>

#include "FileNavigationUseCase.h"
#include "FileNode.h"
#include "NavigationHistory.h"
#include "SearchCriteria.h"
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
    FileListModel* searchResultsModel() const noexcept { return m_searchResultsModel; }
    bool searchActive() const noexcept { return m_searchActive; }
    QString searchQuery() const noexcept { return m_searchQuery; }

    FileListModel* advancedSearchResultsModel() const noexcept { return m_advancedSearchResultsModel; }
    bool advancedSearchActive() const noexcept { return m_advancedSearchActive; }
    const SearchCriteria& advancedSearchCriteria() const noexcept { return m_advancedSearchCriteria; }

    const std::vector<FileNode>& selectedEntries() const noexcept { return m_selectedEntries; }
    SortCriterion sortCriterion() const noexcept;
    bool sortAscending() const noexcept;

    // Pure query, no history/state side effects (unlike navigateTo). Returns the names (not full
    // paths) of every child of `directory` that is itself a directory, sorted case-insensitively.
    // Empty on any listDirectory error (missing/unreadable directory) -- callers treat that as
    // "no suggestions" rather than a navigation failure.
    QStringList suggestFolders(const std::filesystem::path& directory) const;

public slots:
    // Per-tab selection state (Architecture.md §14.9), set by WorkspacePaneWidget from this tab's
    // FileBrowserView::selectionChanged. Cleared on every navigation (see loadAndApply) so a
    // stale selection from the previous folder never leaks into the new one.
    void setSelectedEntries(const std::vector<FileNode>& entries);

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

    // Forwards to the tab's FileListModel, which owns the actual sort state and re-sorts both its
    // current entries and every future listing until changed again.
    void setSortCriterion(SortCriterion criterion, bool ascending);

    // Re-fetches the current directory without touching navigation history. Used after a file
    // operation (copy/move/delete) may have changed a directory's contents out from under a tab
    // that has it open (Architecture.md §14.10).
    void refresh();

    // Recursive filename search scoped to currentPath() (Architecture.md §14.18). No-op if query
    // is empty/whitespace-only. Performs one recursive Port call, caches the snapshot, filters +
    // sorts it into searchResultsModel(), and flips searchActive on. On failure, emits
    // searchFailed and leaves any prior search state untouched.
    void startSearch(const QString& query);

    // Re-filters the cached snapshot from the last startSearch() call — no disk I/O. No-op if
    // searchActive() is false.
    void updateSearchQuery(const QString& query);

    // Clears search state and flips searchActive off. No-op if already inactive.
    void exitSearch();

    // Advanced (criteria) search pane (Architecture.md §14.19), parallel to the quick-search block
    // above. Reveals the pane without scanning yet; exits quick search first (single-active-mode
    // invariant).
    void showAdvancedSearchPanel();

    // No-op if criteria is entirely empty (nameQuery/size bounds/extensions all unset). Otherwise
    // runs one recursive Port call, caches the snapshot, filters + sorts it into
    // advancedSearchResultsModel(), and flips advancedSearchActive on. Exits quick search first. On
    // failure, emits advancedSearchFailed and leaves prior state untouched.
    void startAdvancedSearch(const SearchCriteria& criteria);

    // Re-filters the cached snapshot from the last startAdvancedSearch() call — no disk I/O. No-op
    // if advancedSearchActive() is false.
    void updateAdvancedSearchCriteria(const SearchCriteria& criteria);

    // Clears advanced-search state and flips advancedSearchActive off. No-op if already inactive.
    void exitAdvancedSearch();

signals:
    void currentPathChanged(const std::filesystem::path& path);
    void directoryContentsChanged(const std::filesystem::path& path, const std::vector<FileNode>& entries);
    void backAvailableChanged(bool available);
    void forwardAvailableChanged(bool available);
    void upAvailableChanged(bool available);
    void navigationFailed(const std::filesystem::path& path, const QString& message);
    void viewModeChanged(ViewMode mode);
    void selectedEntriesChanged(const std::vector<FileNode>& entries);
    void sortOrderChanged(SortCriterion criterion, bool ascending);

    void searchModeChanged(bool active);
    void searchResultsChanged(const std::vector<FileNode>& entries);
    void searchFailed(const QString& message);

    void advancedSearchModeChanged(bool active);
    void advancedSearchResultsChanged(const std::vector<FileNode>& entries);
    void advancedSearchFailed(const QString& message);

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
    std::vector<FileNode> m_selectedEntries;

    FileListModel* m_searchResultsModel = nullptr;
    bool m_searchActive = false;
    std::filesystem::path m_searchRoot;
    std::vector<FileNode> m_searchSnapshot;
    QString m_searchQuery;

    FileListModel* m_advancedSearchResultsModel = nullptr;
    bool m_advancedSearchActive = false;
    SearchCriteria m_advancedSearchCriteria;
    std::filesystem::path m_advancedSearchRoot;
    std::vector<FileNode> m_advancedSearchSnapshot;
};
