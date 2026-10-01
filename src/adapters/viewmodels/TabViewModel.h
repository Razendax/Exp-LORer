#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>

#include "DriveSpaceInfo.h"
#include "FileNavigationUseCase.h"
#include "FileNode.h"
#include "FileTagsByPath.h"
#include "FolderMonitorUseCase.h"
#include "FolderSizesByPath.h"
#include "GitRepositorySummary.h"
#include "NavigationHistory.h"
#include "SearchCriteria.h"
#include "Tag.h"
#include "TagManagementUseCase.h"
#include "ViewMode.h"

class FileNavigationUseCase;
class TagManagementUseCase;
class FileListModel;
class FileDecorationRules;
class GitStatusService;
class AdvancedSearchService;
class FileTagsService;
class TagChangeNotifier;
class DiskUsageService;

// Binds address-bar/navigation-toolbar UI to FileNavigationUseCase and NavigationHistory for a
// single tab. Owns the FileListModel that backs that tab's FileBrowserView, wiring
// directoryContentsChanged -> FileListModel::setEntries internally so callers don't have to.
// Per-tab content ViewModel owned by WorkspacePaneViewModel (Architecture.md §14).
class TabViewModel : public QObject
{
    Q_OBJECT

public:
    // `fileDecorationsChangeSource` is threaded through only as a generic QObject& -- see
    // FileDecorationsViewModel.h for why FileListModel/TabViewModel/WorkspacePaneViewModel/
    // WorkspaceController deliberately don't reference that concrete type.
    TabViewModel(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                 const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                 GitStatusService& gitStatusService, FolderMonitorUseCase& folderMonitorUseCase,
                 AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                 DiskUsageService& diskUsageService, TagChangeNotifier& tagChangeNotifier, QObject* parent = nullptr);

    ~TabViewModel() override;

    std::filesystem::path currentPath() const;
    bool backAvailable() const;
    bool forwardAvailable() const;
    bool upAvailable() const;
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

    // Delegates to TagManagementUseCase::searchTags; empty vector on repository failure.
    std::vector<Tag> searchTagsForCriteria(const QString& query) const;

    // Resolves advancedSearchCriteria().tagIds into full Tag objects (for chip display); filters
    // TagManagementUseCase::allTags(), empty vector on repository failure.
    std::vector<Tag> resolveTagCriteria() const;

    // Drive/volume info for the window status bar (Architecture.md §14.30); delegates to
    // FileNavigationUseCase::driveSpaceInfo(currentPath()).
    Result<DriveSpaceInfo> driveSpaceInfo() const;

    // Cached-read git branch/change summary for the status bar (Architecture.md §14.34), kept in
    // sync with currentPath() by requestGitRepositorySummary -- same immediate-sync-on-tab-switch
    // role driveSpaceInfo() fills for drive info, but cached rather than a live call since the
    // underlying data comes back asynchronously off GitStatusService's worker thread.
    GitRepositorySummary lastGitRepositorySummary() const noexcept { return m_lastGitRepositorySummary; }

    // Fetches the current repository's local branch names on demand (not kept live) for the
    // branch-picker menu. No-op success-with-empty-list if the current directory isn't inside a
    // repository; otherwise forwards to GitStatusService.
    void requestLocalBranches(std::function<void(Result<std::vector<std::string>>)> callback);

    // Checks out `branchName` in the current repository. No-op if it already equals the cached,
    // non-detached headDisplayName(). On success, refresh()es the tab (re-triggers the
    // directoryContentsChanged funnel, refreshing the file list, git-status badges, and the branch
    // summary together). On failure, emits gitOperationFailed instead.
    void checkoutBranch(const QString& branchName);

    // Recursive folder monitoring (Architecture.md §14.24, "Monitoring panel"). Multiple tabs may
    // monitor concurrently; only one is ever *displayed* at a time by MonitoringPanelViewModel.
    bool isMonitoringActive() const noexcept { return m_monitoringActive; }
    std::filesystem::path monitoringRoot() const { return m_monitoringRoot; }
    bool isMonitoringPinned() const noexcept { return m_monitoringPinned; }
    const std::deque<FolderChangeEvent>& monitoringLog() const noexcept { return m_monitoringLog; }
    int monitoringTotal() const noexcept { return m_monitoringTotal; }

    // Navigation-guard pure queries (no I/O, no side effects) used by WorkspacePaneWidget's
    // monitoring-scope guard.
    std::filesystem::path upTarget() const;
    std::optional<std::filesystem::path> peekBack() const;
    std::optional<std::filesystem::path> peekForward() const;

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

    // Moves the tab to `path` as a replacement for its current history entry rather than a new
    // one -- used when the folder the tab is showing (or one of its ancestors) was renamed or
    // deleted out from under it (Architecture.md §14.41). Same validation/failure signalling as
    // navigateTo(); exits any active search.
    void replaceCurrentPath(const std::filesystem::path& path);

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
    // above. Reveals the pane with a plain non-recursive listing of the current folder (not a real
    // search -- no snapshot is cached, no criteria applied) so it doesn't open empty; exits quick
    // search first (single-active-mode invariant). The first real search still scans recursively.
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

    // Adds tagId to the advanced-search criteria (no-op if already present), opens the panel if
    // it isn't already (single-active-mode invariant, same as showAdvancedSearchPanel), and
    // (re)runs the search. Used by both the tag panel's chip-click path (requirement 1/2) and
    // Advanced Search's own tag-search box (requirement 3).
    void addTagSearchCriterion(Tag::Id tagId);

    // Removes tagId from the criteria and re-runs the search if the panel is active; no-op
    // otherwise or if tagId isn't present.
    void removeTagSearchCriterion(Tag::Id tagId);

    // No-op if already active or already starting, or if currentPath() is VirtualPaths::ThisPC/an
    // archive-virtual path (the Start button is also UI-disabled in that state, but this slot stays
    // defensive). Clears any previous session's log/total, then dispatches an async watch request
    // for currentPath() -- opening the directory handle is I/O and does not happen synchronously
    // here (Architecture.md's no-I/O-on-UI-thread rule), so isMonitoringActive() only flips true
    // once that open actually succeeds; monitoringFailed is emitted instead on failure.
    void startMonitoring();

    // No-op if not active and no start is currently in flight. If a start dispatched moments ago
    // hasn't finished opening the directory handle yet, cancels it instead (its completion callback
    // stops the monitor immediately if the open still ends up succeeding). Log/total are
    // deliberately left untouched otherwise (a later startMonitoring() call clears them) so the
    // last session's results stay visible after stopping.
    void stopMonitoring();

    // No-op if unchanged, or if not currently active (nothing to pin).
    void setMonitoringPinned(bool pinned);

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

    // Emitted right before dispatching a recursive-scan or tag-first-candidate request to
    // AdvancedSearchService, so the UI can show a busy state (Architecture.md §14.x).
    void advancedSearchLoading();

    // Emitted once the in-flight scan/candidate fetch that most recently triggered
    // advancedSearchLoading() actually finishes (successfully -- advancedSearchFailed covers the
    // error case). Unlike advancedSearchResultsChanged, this does NOT fire for the intermediate
    // progress ticks a long recursive scan reports along the way (Architecture.md §14.37), so a
    // busy indicator wired to loading/this pair stays up for the whole scan rather than clearing
    // after the first partial result.
    void advancedSearchLoadingFinished();

    // Lets the UI re-sync its tag-criteria chip row (and, harmlessly, its Name/size/extension
    // fields, already idempotent via QSignalBlocker in SearchCriteriaPanel::setCriteria)
    // regardless of *which* entry point changed the criteria (its own fields, its own tag-search
    // box, or a right-panel tag click on a completely different widget).
    void advancedSearchCriteriaChanged(const SearchCriteria& criteria);

    void monitoringActiveChanged(bool active);
    void monitoringLogAppended(const std::vector<FolderChangeEvent>& batch);
    void monitoringTotalChanged(int total);
    void monitoringPinnedChanged(bool pinned);
    void monitoringFailed(const QString& message);

    // Git branch indicator/checkout (Architecture.md §14.34).
    void gitRepositorySummaryChanged(const GitRepositorySummary& summary);
    void gitOperationFailed(const QString& message);

private:
    // Single funnel point for every navigation entry point: fetches directory contents exactly
    // once, and on success emits currentPathChanged + directoryContentsChanged, recording history
    // only when recordHistory is true (false for goBack/goForward, which only move the existing
    // history pointer rather than recording a new entry).
    enum class HistoryUpdate
    {
        None,           // back/forward/refresh: history already points at `path`
        Record,         // a new navigation step
        ReplaceCurrent, // current entry swapped in place (replaceCurrentPath)
    };
    void loadAndApply(const std::filesystem::path& path, HistoryUpdate historyUpdate);
    void emitAvailability();

    // Connected to directoryContentsChanged (normal browsing only -- not the search result
    // signals, per the v1 scope cut). Dispatches a background git-status scan for `path` and
    // applies the result to m_fileListModel once it comes back, dropping stale replies for a path
    // that's no longer current (Architecture.md §14.x).
    void requestGitStatus(const std::filesystem::path& path);

    // Wired into the same directoryContentsChanged funnel as requestGitStatus -- dispatches a
    // background repository-summary fetch for `path` and emits gitRepositorySummaryChanged once it
    // comes back, dropping stale replies for a path that's no longer current (Architecture.md
    // §14.34).
    void requestGitRepositorySummary(const std::filesystem::path& path);

    // Wired into the same directoryContentsChanged funnel as requestGitStatus, plus
    // m_tagChangeNotifier's tagsChanged() signal (Architecture.md §14.x, Tags column). Dispatches
    // through m_fileTagsService (its own background thread + SQLite connection, same shape as
    // requestGitStatus), dropping a reply for a path that's no longer current the same way
    // requestGitStatus does.
    void requestFileTags(const std::filesystem::path& path, const std::vector<FileNode>& entries);

    // Wired into the same directoryContentsChanged funnel as requestGitStatus/requestFileTags
    // (Architecture.md §14.39). A read-only cached lookup -- never triggers a scan -- via
    // DiskUsageService::requestChildren (the same call FolderSizeViewModel's pie chart uses); a
    // no-op fast path skips the round trip entirely when `entries` has no subdirectories. Dropping a
    // reply for a path that's no longer current the same way requestGitStatus does.
    void requestFolderSizes(const std::filesystem::path& path, const std::vector<FileNode>& entries);

    // Fetches tags for every file in `candidates` and, once they arrive (discarded if
    // `generation` no longer matches m_searchGeneration), caches them into m_searchSnapshotTags and
    // applies them to m_searchResultsModel. Used whenever the search snapshot itself changes
    // (onSearchScanReady) -- updateSearchQuery's own re-filters reuse the cached map directly
    // instead of re-dispatching (its filtered subset is always a subset of the same snapshot).
    void requestSearchResultTags(int generation, const std::vector<FileNode>& candidates);

    // Same shape as requestSearchResultTags, for the advanced-search side: fetches tags for
    // `candidates`, discarded if `generation` no longer matches m_advancedSearchGeneration, then
    // caches into m_advancedSearchSnapshotTags and applies to m_advancedSearchResultsModel. Used
    // whenever the candidate set itself changes (a new full/tag-first snapshot, a progress tick, or
    // showAdvancedSearchPanel's initial listing) -- applyAdvancedSearchFilter's own re-filters reuse
    // the cached map directly.
    void requestAdvancedSearchResultTags(int generation, const std::vector<FileNode>& candidates);

    // Shared implementation of startAdvancedSearch/updateAdvancedSearchCriteria/
    // addTagSearchCriterion/removeTagSearchCriterion. When criteria.tagIds is non-empty, the
    // search is tag-first: queries TagManagementUseCase::findFilesWithAllTags directly (no
    // recursive scan), scopes the result to currentPath() via isPathWithinOrEqual, stat()s each
    // surviving path, and applies filterByCriteria as a secondary name/size filter. Otherwise,
    // re-scans recursively only when forceRescan or no valid cached scan exists yet
    // (m_advancedSearchFullScanDone), then applies filterByCriteria. Both branches converge on
    // sortBy -> advancedSearchResultsModel()->setEntries and emit the result + criteria-changed
    // signals (Architecture.md §14.22).
    void runAdvancedSearch(bool forceRescan);

    // Composes filterByCriteria -> sortBy -> setEntries -> activate-if-needed -> emit
    // advancedSearchResultsChanged/advancedSearchCriteriaChanged, the tail shared by the
    // synchronous cached-snapshot path and both async completion callbacks below.
    void applyAdvancedSearchFilter(const std::vector<FileNode>& candidates);

    // Completion callback for AdvancedSearchService::requestRecursiveScan/
    // requestTagFirstCandidates. Discards the result if `generation` no longer matches
    // m_advancedSearchGeneration (a newer request superseded it, or the search/tab was closed in
    // the meantime). isNewSnapshot distinguishes a fresh recursive-scan result (cached into
    // m_advancedSearchSnapshot) from a tag-first result (applied directly, never cached).
    void onAdvancedSearchCandidatesReady(int generation, Result<std::vector<FileNode>> result, bool isNewSnapshot);

    // Progress callback for AdvancedSearchService::requestRecursiveScan's onProgress
    // (Architecture.md §14.37): discarded the same way as onAdvancedSearchCandidatesReady via the
    // generation check. Caches `partial` as the best-known candidate list so far and re-filters it
    // with whatever m_advancedSearchCriteria holds *right now* -- so a criteria edit that arrives
    // while the scan is still running is reflected on the very next tick without needing its own
    // rescan (see the "reuse the in-flight scan" branch of runAdvancedSearch).
    void onAdvancedSearchProgress(int generation, std::vector<FileNode> partial);

    // Completion callback for the recursive scan dispatched by startSearch() (via
    // AdvancedSearchService::requestRecursiveScan, same off-UI-thread scan the Advanced Search pane
    // uses). Discards the result if `generation` no longer matches m_searchGeneration (a newer
    // startSearch() call, or exitSearch()/tab close, superseded it in the meantime).
    void onSearchScanReady(int generation, Result<std::vector<FileNode>> result);

    FileNavigationUseCase& m_fileNavigationUseCase;
    TagManagementUseCase& m_tagManagementUseCase;
    GitStatusService& m_gitStatusService;
    FolderMonitorUseCase& m_folderMonitorUseCase;
    AdvancedSearchService& m_advancedSearchService;
    FileTagsService& m_fileTagsService;
    DiskUsageService& m_diskUsageService;
    TagChangeNotifier& m_tagChangeNotifier;
    NavigationHistory m_history;
    ViewMode m_viewMode = ViewMode::Details;
    FileListModel* m_fileListModel = nullptr;
    std::vector<FileNode> m_selectedEntries;

    FileListModel* m_searchResultsModel = nullptr;
    bool m_searchActive = false;
    std::filesystem::path m_searchRoot;
    std::vector<FileNode> m_searchSnapshot;
    // Tags for every file in m_searchSnapshot (Architecture.md §14.x, Tags column), fetched
    // whenever the snapshot itself changes (onSearchScanReady) via requestSearchResultTags and
    // reapplied as-is by every updateSearchQuery() re-filter -- a filtered subset never needs a
    // fresh fetch since it's always a subset of the same snapshot.
    TagsByPathMap m_searchSnapshotTags;
    QString m_searchQuery;
    // Bumped on every startSearch() dispatch and on exitSearch(); onSearchScanReady discards any
    // reply whose generation no longer matches, same discard-by-generation-id approach as
    // m_advancedSearchGeneration above.
    int m_searchGeneration = 0;

    FileListModel* m_advancedSearchResultsModel = nullptr;
    bool m_advancedSearchActive = false;
    SearchCriteria m_advancedSearchCriteria;
    std::filesystem::path m_advancedSearchRoot;
    std::vector<FileNode> m_advancedSearchSnapshot;
    // True only right after a successful listDirectoryRecursive has populated
    // m_advancedSearchSnapshot. m_advancedSearchRoot.empty() alone can no longer signal "have we
    // ever scanned", since the tag-first branch also sets m_advancedSearchRoot (for scoping)
    // without ever performing a recursive scan.
    bool m_advancedSearchFullScanDone = false;
    // True from the moment a recursive scan is dispatched until its completion callback runs (with
    // a matching generation) or exitAdvancedSearch() resets it. While true, a criteria edit
    // (updateAdvancedSearchCriteria/removeTagSearchCriterion, forceRescan=false) does NOT dispatch
    // a second concurrent scan of the same root -- it just re-filters m_advancedSearchPartialSnapshot
    // synchronously, since the one scan already running will itself keep reporting progress (and
    // eventually the final result) filtered against whatever criteria is current when each tick
    // fires (Architecture.md §14.37).
    bool m_advancedSearchScanPending = false;
    // Best-known candidate list from the in-flight scan's progress ticks (Architecture.md §14.37),
    // updated by onAdvancedSearchProgress. Distinct from m_advancedSearchSnapshot, which is only
    // ever written once a scan has *fully* completed.
    std::vector<FileNode> m_advancedSearchPartialSnapshot;
    // Tags for whichever candidate list (m_advancedSearchSnapshot or m_advancedSearchPartialSnapshot)
    // was most recently fetched, fetched via requestAdvancedSearchResultTags whenever that candidate
    // list itself changes and reapplied as-is by every applyAdvancedSearchFilter re-filter.
    TagsByPathMap m_advancedSearchSnapshotTags;
    // The tagIds that m_advancedSearchSnapshot/m_advancedSearchFullScanDone/m_advancedSearchScanPending
    // currently pertain to when they were populated by the tag-first branch of runAdvancedSearch
    // (empty when they instead pertain to a plain recursive scan of m_advancedSearchRoot). Lets a
    // Name/size-only criteria edit with the same tag selection reuse the cached/in-flight tag-first
    // candidates instead of re-dispatching a redundant DB query + per-file stat loop.
    std::vector<Tag::Id> m_advancedSearchCachedTagIds;
    // Bumped every time a recursive-scan or tag-first-candidate request is dispatched, and on
    // exitAdvancedSearch(); onAdvancedSearchCandidatesReady/onAdvancedSearchProgress discard any
    // reply whose generation no longer matches, so a stale result from a superseded/abandoned
    // request can't resurrect it (Architecture.md §5's discard-by-generation-id approach, same as
    // FilePreviewViewModel).
    int m_advancedSearchGeneration = 0;

    // True from startMonitoring() dispatching StandardFolderMonitor's async open until that open's
    // onStarted callback runs (successfully or not) -- opening the directory handle is I/O and now
    // happens on IFolderMonitor's own worker thread rather than inline, so there is a real gap
    // where a monitor has been requested but m_monitoringActive is still false. Guards against a
    // redundant second startMonitoring() dispatch during that gap.
    bool m_monitoringStarting = false;
    // Set by stopMonitoring() if it runs during that gap (there is no FolderMonitorId yet to stop
    // via m_folderMonitorUseCase) -- checked by the onStarted completion callback, which stops the
    // monitor immediately instead of activating it if this is set.
    bool m_monitoringCancelled = false;
    bool m_monitoringActive = false;
    std::filesystem::path m_monitoringRoot;
    FolderMonitorId m_monitoringId = 0;
    bool m_monitoringPinned = false;
    std::deque<FolderChangeEvent> m_monitoringLog; // capped at kMonitoringLogCap, oldest dropped
    int m_monitoringTotal = 0; // uncapped

    GitRepositorySummary m_lastGitRepositorySummary;
};
