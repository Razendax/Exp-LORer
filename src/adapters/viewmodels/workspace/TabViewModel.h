#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "DriveSpaceInfo.h"
#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "FileNode.h"
#include "FileTagsByPath.h"
#include "FolderMonitorUseCase.h"
#include "FolderSizesByPath.h"
#include "GitRepositorySummary.h"
#include "MediaThumbnailLoadingService.h"
#include "MediaViewerKind.h"
#include "MediaViewerSortCriterion.h"
#include "MediaViewerTagBehaviour.h"
#include "MediaViewerViewMode.h"
#include "NameSearchOptions.h"
#include "NavigationHistory.h"
#include "NotificationCenter.h"
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
class FragmentationService;
class IconLoadingService;

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
                 DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                 IconLoadingService& iconLoadingService, MediaThumbnailLoadingService& mediaThumbnailLoadingService,
                 TagChangeNotifier& tagChangeNotifier, NotificationCenter& notificationCenter, QObject* parent = nullptr);

    ~TabViewModel() override;

    std::filesystem::path currentPath() const;
    bool backAvailable() const;
    bool forwardAvailable() const;
    bool upAvailable() const;
    ViewMode viewMode() const noexcept { return m_viewMode; }

    // Per-column hide/show override for this tab (Architecture.md §14.49). nullopt slots defer to
    // Settings > General > Columns' "default visibility for new tab"
    // (ColumnVisibilitySettingsViewModel::defaultVisibility); see TabConfig::hiddenColumns for the
    // full tri-state rationale.
    const std::array<std::optional<bool>, FileListModel::ColumnCount>& hiddenColumns() const noexcept
    {
        return m_hiddenColumns;
    }

    FileListModel* fileListModel() const noexcept { return m_fileListModel; }
    FileListModel* searchResultsModel() const noexcept { return m_searchResultsModel; }
    bool searchActive() const noexcept { return m_searchActive; }
    QString searchQuery() const noexcept { return m_searchQuery; }

    // Quick Search tab's Match Case/Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles
    // (Architecture.md §14.44/§14.45), per-tab and not persisted across restarts (same posture as
    // m_searchQuery/View as tree).
    NameSearchOptions searchOptions() const noexcept
    {
        return { m_searchMatchMode, m_searchCaseSensitive, m_searchSkipGitIgnored };
    }

    FileListModel* advancedSearchResultsModel() const noexcept { return m_advancedSearchResultsModel; }
    bool advancedSearchActive() const noexcept { return m_advancedSearchActive; }
    const SearchCriteria& advancedSearchCriteria() const noexcept { return m_advancedSearchCriteria; }

    // Media Viewer mode (Architecture.md §14.61): a focused, full-pane recursive gallery of every
    // image/video under whatever folder was current when it was entered.
    FileListModel* mediaViewerResultsModel() const noexcept { return m_mediaViewerResultsModel; }
    bool mediaViewerActive() const noexcept { return m_mediaViewerActive; }
    MediaViewerKind mediaViewerKind() const noexcept { return m_mediaViewerKind; }
    const std::vector<Tag::Id>& mediaViewerTagFilters() const noexcept { return m_mediaViewerTagIds; }
    QString mediaViewerNameFilter() const noexcept { return m_mediaViewerNameQuery; }
    NameSearchOptions mediaViewerNameOptions() const noexcept { return m_mediaViewerNameOptions; }
    MediaViewerSortCriterion mediaViewerSortCriterion() const noexcept { return m_mediaViewerSortCriterion; }
    bool mediaViewerSortAscending() const noexcept { return m_mediaViewerSortAscending; }
    MediaViewerViewMode mediaViewerViewMode() const noexcept { return m_mediaViewerViewMode; }
    MediaViewerTagBehaviour mediaViewerTagBehaviour() const noexcept { return m_mediaViewerTagBehaviour; }

    // Resolves mediaViewerTagFilters() into full Tag objects (for chip display), mirroring
    // resolveTagCriteria().
    std::vector<Tag> resolveMediaViewerTagFilters() const;

    const std::vector<FileNode>& selectedEntries() const noexcept { return m_selectedEntries; }
    SortCriterion sortCriterion() const noexcept;
    bool sortAscending() const noexcept;

    // Pure query, no history/state side effects (unlike navigateTo). Returns the names (not full
    // paths) of every child of `directory` that is itself a directory, sorted case-insensitively.
    // Empty on any listDirectory error (missing/unreadable directory) -- callers treat that as
    // "no suggestions" rather than a navigation failure.
    QStringList suggestFolders(const std::filesystem::path& directory) const;

    // Pure query, no history/state side effects. Delegates to
    // FileNavigationUseCase::expandEnvironmentVariables (Architecture.md §14.54).
    std::filesystem::path expandEnvironmentVariables(const std::filesystem::path& input) const;

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

    // Re-requests icons for whatever's currently loaded (normal browsing, and quick/advanced
    // search results if active) when the new mode's icon size tier (IconSizeMask.h) differs from
    // the old one, so switching e.g. Details -> Large Icons doesn't leave rows showing an upscaled
    // 16px icon forever. Cheap even for a huge directory: IconLoadingWorker merges a missing size
    // into its already-cached record rather than re-resolving sizes it already has.
    void setViewMode(ViewMode mode);

    // Bulk-replaces the per-tab column-visibility overrides, used by WorkspaceController::
    // restoreFromConfig to apply a restored TabConfig::hiddenColumns in one shot. No signal --
    // nothing currently needs to react live to this copy changing, only read it back later via
    // hiddenColumns() at capture time.
    void setHiddenColumns(const std::array<std::optional<bool>, FileListModel::ColumnCount>& hiddenColumns);

    // Records this tab's own explicit hide/show choice for `column` (Architecture.md §14.49),
    // used by WorkspacePaneWidget's live FileBrowserView::columnVisibilityChanged connection so
    // this copy always reflects the header-menu state the moment the user toggles a column.
    void setColumnHidden(FileListModel::Column column, bool hidden);

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

    // Recursive filename search scoped to currentPath() (Architecture.md §14.42), async via
    // AdvancedSearchService -- same in-flight-scan-reuse posture as Advanced Search (Architecture.md
    // §14.37/§14.38). Empty/whitespace-only query exits search (no-op if already inactive).
    // Otherwise: the first call against a given root dispatches one recursive scan; an edit that
    // arrives before that scan finishes re-filters its latest progress tick in memory instead of
    // starting a second concurrent scan of the same root; an edit after it finishes re-filters the
    // cached snapshot, no disk I/O. On a scan failure, emits searchFailed and leaves prior search
    // state untouched.
    void updateSearchQuery(const QString& query);

    // Forces a fresh recursive rescan of currentPath() against `query` (Refresh button / Enter in
    // the search bar). Unlike the old no-arg form, this always (re)starts a search scoped to
    // wherever the tab is *currently* browsing, even if a prior search was exited by an in-tab
    // navigation (searchActive() == false) -- the search bar's text can outlive navigateTo()'s
    // exitSearch() without this being a no-op. Empty/whitespace-only query exits search instead.
    void refreshSearch(const QString& query);

    // Updates the Match Case/Whole Word/Exact Name/Match Regex toggles (Architecture.md §14.44) and
    // re-filters whatever candidate list (cached snapshot, or an in-flight scan's latest partial
    // snapshot) is already available in memory -- never dispatches a rescan, since the option
    // change doesn't affect which root/candidate set is being searched, only how it's filtered. A
    // no-op beyond updating the two members if the search query is currently empty (nothing to
    // filter yet) -- the new options still take effect the next time a query is typed.
    void setSearchOptions(const NameSearchOptions& options);

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

    // Media Viewer mode (Architecture.md §14.61): a focused, full-pane recursive gallery of every
    // image/video under currentPath(), entered from the View menu. No-op if already active.
    // Captures currentPath() as the fixed recursive root, resets kind/filters to their defaults,
    // exits Quick/Advanced Search first (single-active-mode invariant, same posture as
    // showAdvancedSearchPanel), dispatches the initial candidate fetch, and emits
    // mediaViewerModeChanged(true).
    void enterMediaViewer();

    // Clears Media Viewer state and flips mediaViewerActive off. No-op if already inactive.
    void exitMediaViewer();

    // Switches the grid between images and videos -- re-filters the cached candidate set in
    // memory only (no rescan: the cached set is already post-tag-filter, pre-kind/name-filter).
    // No-op if already active or outside Media Viewer mode.
    void setMediaViewerKind(MediaViewerKind kind);

    // Adds/removes a tag from the AND-combined tag filter (no-op if already present/absent) and
    // re-dispatches the candidate fetch, since the tag set determines which candidates are even
    // fetched (tag-first vs. recursive-scan branch of runMediaViewerQuery()). No-op outside Media
    // Viewer mode.
    void addMediaViewerTagFilter(Tag::Id tagId);
    void removeMediaViewerTagFilter(Tag::Id tagId);

    // Media Viewer mode's tag behaviour toggle (Architecture.md §14.72): flips what a plain click
    // vs. a Ctrl+click on a tag chip/pill does in the two slots below. Not reset by
    // enterMediaViewer()/exitMediaViewer() -- a per-tab UI preference, same posture as
    // m_mediaViewerSortCriterion/m_mediaViewerViewMode.
    void setMediaViewerTagBehaviour(MediaViewerTagBehaviour behaviour);

    // The search-results tag-chip click (MediaViewerSearchPanelWidget's matching-tag row): XOR's
    // ctrlHeld against the current behaviour to decide between adding tagId to the filter set and
    // applying it to the current selection. No-op outside Media Viewer mode.
    void handleMediaViewerSearchTagActivated(Tag::Id tagId, bool ctrlHeld);

    // Assigns tagId to every entry in m_selectedEntries (ignoring a per-entry AlreadyExists),
    // emitting mediaViewerFailed if the selection is empty and a "Tag Applied" popup + tagsChanged
    // on at least one success. Used by handleMediaViewerSearchTagActivated's "Apply" branch.
    void applyMediaViewerTagToSelection(Tag::Id tagId);

    // The Details-view per-item tag-pill click: same XOR rule as above, but the "other" action is
    // removing tagId from `entry` specifically rather than applying it to the whole selection. No-op
    // outside Media Viewer mode.
    void handleMediaViewerDetailsTagClick(const FileNode& entry, Tag::Id tagId, bool ctrlHeld);

    // Updates the name/Match Case/Whole Word/Exact Name/Match Regex filter and re-filters the
    // cached candidate set in memory only -- never dispatches a rescan, since neither affects which
    // candidates were fetched. No-op outside Media Viewer mode.
    void setMediaViewerNameFilter(const QString& query, const NameSearchOptions& options);

    // Media Viewer mode's sort-by/direction/view controls (Architecture.md §14.68). Each is a
    // no-op if unchanged, else updates the member, emits its signal, and re-sorts the already-
    // fetched candidate set in memory only (reapplyMediaViewerSort) -- never dispatches a rescan,
    // same posture as setMediaViewerNameFilter.
    void setMediaViewerSortCriterion(MediaViewerSortCriterion criterion);
    void setMediaViewerSortAscending(bool ascending);

    // Stores the chosen view and emits the signal; has no behavioral effect yet (the grid stays
    // locked to DisplayMode::MediaViewerGrid either way) -- deliberately stubbed "one option
    // today, more later" per the request.
    void setMediaViewerViewMode(MediaViewerViewMode mode);

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

    // Emitted right before dispatching a recursive-scan request for the Search tab's own quick
    // search (Architecture.md §14.42), mirroring advancedSearchLoading below.
    void searchLoading();

    // Emitted once the in-flight scan that most recently triggered searchLoading() actually
    // finishes (successfully -- searchFailed covers the error case); does NOT fire for a long
    // scan's intermediate progress ticks, same posture as advancedSearchLoadingFinished.
    void searchLoadingFinished();

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

    // Media Viewer mode (Architecture.md §14.61).
    void mediaViewerModeChanged(bool active);
    void mediaViewerResultsChanged(const std::vector<FileNode>& entries);
    void mediaViewerKindChanged(MediaViewerKind kind);
    // Lets the UI re-sync its applied-tag-filter chip row regardless of which entry point changed
    // the filter set (mirrors advancedSearchCriteriaChanged).
    void mediaViewerCriteriaChanged();
    // Media Viewer mode's toolbar sort/view controls (Architecture.md §14.68).
    void mediaViewerSortCriterionChanged(MediaViewerSortCriterion criterion);
    void mediaViewerSortAscendingChanged(bool ascending);
    void mediaViewerViewModeChanged(MediaViewerViewMode mode);
    void mediaViewerTagBehaviourChanged(MediaViewerTagBehaviour behaviour);
    void mediaViewerLoading();
    void mediaViewerLoadingFinished();
    void mediaViewerFailed(const QString& message);

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

    // Shared by showAdvancedSearchPanel/startAdvancedSearch/updateAdvancedSearchCriteria/
    // removeTagSearchCriterion for the "criteria is (now) empty" case (Architecture.md §14.56):
    // shows a plain non-recursive listing of the current folder and resets every cached-scan
    // member (mirrors exitAdvancedSearch's cleanup) while keeping m_advancedSearchActive true, so
    // the pane stays open instead of closing. Emits advancedSearchResultsChanged/
    // advancedSearchCriteriaChanged at the end so the status bar count and the panel's tag-chip
    // row stay in sync even when reached from a live edit rather than showAdvancedSearchPanel
    // itself. On a listDirectory failure, emits advancedSearchFailed and leaves prior state
    // untouched, same as showAdvancedSearchPanel did before this was extracted.
    void showCurrentFolderNonRecursive();

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

    // Wired into the same directoryContentsChanged funnel as requestFolderSizes (Architecture.md
    // §14.53). A read-only cached lookup -- never triggers a scan -- via
    // FragmentationService::requestFilesUnder, the same cache-only lookup the Fragmentation
    // bottom-panel tab uses for its own cache-only redisplay; a no-op fast path skips the round
    // trip entirely when `entries` has no plain files. Dropping a reply for a path that's no
    // longer current the same way requestFolderSizes does.
    void requestFragmentCounts(const std::filesystem::path& path, const std::vector<FileNode>& entries);

    // Wired into the same directoryContentsChanged funnel as requestGitStatus/requestFileTags/
    // requestFolderSizes/requestFragmentCounts ("Fix slow folder-open for executable-heavy
    // directories"). Dispatches a chunked background icon-resolution request via
    // IconLoadingService, applying each chunk's results to m_fileListModel as they arrive --
    // dropping a reply for a path that's no longer current the same way requestGitStatus does.
    void requestIcons(const std::filesystem::path& path, const std::vector<FileNode>& entries);

    // Same mechanism as requestIcons, for the quick-search results model -- called wherever
    // requestSearchResultTags already is (i.e. whenever the search candidate list itself changes),
    // mirroring how that function covers m_searchResultsModel. Discarded if `generation` no longer
    // matches m_searchGeneration, same discard-by-generation-id approach as
    // onSearchCandidatesReady/onSearchProgress.
    void requestSearchResultIcons(int generation, const std::vector<FileNode>& candidates);

    // Same as requestSearchResultIcons, for the advanced-search results model -- called wherever
    // requestAdvancedSearchResultTags already is. Discarded if `generation` no longer matches
    // m_advancedSearchGeneration.
    void requestAdvancedSearchResultIcons(int generation, const std::vector<FileNode>& candidates);

    // Same as requestAdvancedSearchResultIcons, for the Media Viewer results model -- called once
    // per candidate-set fetch (onMediaViewerCandidatesReady), not on every in-memory kind/name
    // filter edit (same "icons only follow a real fetch" posture requestAdvancedSearchResultIcons
    // already has). Discarded if `generation` no longer matches m_mediaViewerGeneration.
    void requestMediaViewerResultIcons(int generation, const std::vector<FileNode>& candidates);

    // Same as requestMediaViewerResultIcons, for the Media Viewer grid's real decoded thumbnails
    // (Architecture.md §14.62) rather than the plain DecorationRole icon -- called once per
    // candidate-set fetch (onMediaViewerCandidatesReady), same "only follows a real fetch" posture.
    // Discarded if `generation` no longer matches m_mediaViewerGeneration.
    //
    // Every delivered batch advances the Loading Media Viewer progress notification's determinate
    // phase (Architecture.md §14.66), whenever one is active (m_mediaViewerProgressId != 0) --
    // not just requestKindSweep's own Low-priority sweep dispatch. applyMediaViewerFilter's High-
    // priority call (the kind+name-filtered subset, issued first so it jumps the queue ahead of an
    // in-flight sweep) counts too: with no active name filter that subset is most or all of the
    // kind, so leaving it uncounted left the bar stuck at 0 while the grid was already filling in
    // (see "Progress-counting correctness" in the decision log). Counting is de-duplicated by path
    // (m_mediaViewerCountedPaths) so the same file decoded by both the High and Low dispatch is
    // only ever counted once. `priority` is forwarded as-is to MediaThumbnailLoadingService::
    // requestThumbnails.
    void requestMediaViewerResultThumbnails(int generation, const std::vector<FileNode>& candidates,
                                             MediaThumbnailLoadingService::Priority priority);

    // Extra Large Icons only (Architecture.md §14.71): mirrors requestMediaViewerResultThumbnails's
    // shape, but scoped to the normal-browsing model (m_fileListModel) and gated to image/video
    // entries only -- unlike Media Viewer's candidates, which are already media-only by
    // construction, `entries` here is a plain directory listing. No-op if `entries` has no
    // image/video files. Called from the same two places requestIcons(path, entries) already is,
    // both gated to ViewMode::ExtraLargeIcons. Discarded if `path` no longer matches currentPath(),
    // the same staleness guard requestIcons() uses.
    void requestBrowsingThumbnails(const std::filesystem::path& path, const std::vector<FileNode>& entries);

    // Fetches tags for every file in `candidates` and, once they arrive (discarded if
    // `generation` no longer matches m_searchGeneration), caches them into m_searchSnapshotTags and
    // applies them to m_searchResultsModel. Used whenever the search candidate set itself changes
    // (a new full/partial snapshot) -- applySearchFilter's own re-filters reuse the cached map
    // directly instead of re-dispatching (its filtered subset is always a subset of the same
    // snapshot).
    void requestSearchResultTags(int generation, const std::vector<FileNode>& candidates);

    // Same shape as requestSearchResultTags, for the advanced-search side: fetches tags for
    // `candidates`, discarded if `generation` no longer matches m_advancedSearchGeneration, then
    // caches into m_advancedSearchSnapshotTags and applies to m_advancedSearchResultsModel. Used
    // whenever the candidate set itself changes (a new full/tag-first snapshot, a progress tick, or
    // showAdvancedSearchPanel's initial listing) -- applyAdvancedSearchFilter's own re-filters reuse
    // the cached map directly.
    void requestAdvancedSearchResultTags(int generation, const std::vector<FileNode>& candidates);

    // Same shape as requestSearchResultTags/requestAdvancedSearchResultTags, for the Media Viewer
    // results model (Architecture.md §14.69's Details view tag row): fetches tags for
    // `candidates`, discarded if `generation` no longer matches m_mediaViewerGeneration, then
    // applies directly to m_mediaViewerResultsModel. Called from applyMediaViewerFilter() alongside
    // the existing icon/thumbnail requests, and from the TagChangeNotifier::tagsChanged handler.
    // Unlike the search/advanced-search siblings, no separate snapshot-tags cache is kept -- Media
    // Viewer's own candidate list already survives in-memory kind/name re-filters without needing
    // a fresh tag fetch (refilterEntries() leaves setFileTags()'s prior result alone).
    void requestMediaViewerResultTags(int generation, const std::vector<FileNode>& candidates);

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

    // Shared implementation of updateSearchQuery/refreshSearch (Architecture.md §14.42), mirroring
    // runAdvancedSearch's non-tag branch: re-scans recursively only when forceRescan or no valid
    // cached/in-flight scan of the current root exists yet, otherwise re-filters whatever candidate
    // list (cached snapshot, or an in-flight scan's latest partial snapshot) is already available in
    // memory -- never a second concurrent scan of the same root.
    void runSearch(bool forceRescan);

    // Composes filterByName -> sortBy -> setEntries -> activate-if-needed -> emit
    // searchResultsChanged, the tail shared by the synchronous cached-snapshot path and both async
    // completion callbacks below. Mirrors applyAdvancedSearchFilter.
    void applySearchFilter(const std::vector<FileNode>& candidates);

    // Completion callback for the recursive scan dispatched by runSearch() (via
    // AdvancedSearchService::requestRecursiveScan, same off-UI-thread scan the Advanced Search pane
    // uses). Discards the result if `generation` no longer matches m_searchGeneration (a newer
    // dispatch, or exitSearch()/tab close, superseded it in the meantime).
    void onSearchCandidatesReady(int generation, Result<std::vector<FileNode>> result);

    // Progress callback for AdvancedSearchService::requestRecursiveScan's onProgress
    // (Architecture.md §14.37), mirroring onAdvancedSearchProgress: discarded the same way as
    // onSearchCandidatesReady via the generation check. Caches `partial` as the best-known
    // candidate list so far and re-filters it with whatever m_searchQuery holds *right now* -- so a
    // query edit that arrives while the scan is still running is reflected on the very next tick.
    void onSearchProgress(int generation, std::vector<FileNode> partial);

    // Dispatches Media Viewer mode's candidate fetch via the already-injected
    // m_advancedSearchService (Architecture.md §14.61) -- tag-first (requestTagFirstCandidates)
    // when m_mediaViewerTagIds is non-empty, else a plain recursive scan
    // (requestRecursiveScan) of m_mediaViewerRoot. Deliberately simpler than
    // runAdvancedSearch/runSearch: no in-flight-scan reuse or progress-tick machinery, since this
    // is only ever called right after the tag filter set itself changed (enterMediaViewer/
    // addMediaViewerTagFilter/removeMediaViewerTagFilter), not on every keystroke. On completion,
    // caches the result into m_mediaViewerCandidates and calls applyMediaViewerFilter().
    void runMediaViewerQuery();

    // Filters m_mediaViewerCandidates by MediaExtensions::isImageExtension/isVideoExtension for
    // m_mediaViewerKind, then by m_mediaViewerNameQuery/m_mediaViewerNameOptions (reusing
    // FileNavigationUseCase::filterByName), sorts, and applies to m_mediaViewerResultsModel. The
    // tail shared by onMediaViewerCandidatesReady and every in-memory kind/name-filter edit.
    // `freshCandidates` selects which FileListModel method applies the filtered result:
    // onMediaViewerCandidatesReady (just replaced m_mediaViewerCandidates from a new scan) passes
    // true, going through setEntries() to drop stale per-path caches from the previous candidate
    // set; a plain in-memory kind/name-filter edit of the SAME candidates passes false, going
    // through refilterEntries() so already-resolved icons/thumbnails survive narrowing and
    // widening the filter without flickering back to their placeholder (Architecture.md §14.62).
    // The kind+name filtering step itself is factored out into filterMediaViewerCandidates() and
    // the sort step into sortMediaViewerResults() (Architecture.md §14.68) so reapplyMediaViewerSort()
    // can re-sort without re-filtering.
    void applyMediaViewerFilter(bool freshCandidates);

    // The kind+name filtering half of applyMediaViewerFilter (Architecture.md §14.68): filters
    // m_mediaViewerCandidates by MediaExtensions::isImageExtension/isVideoExtension for
    // m_mediaViewerKind, then by m_mediaViewerNameQuery/m_mediaViewerNameOptions.
    std::vector<FileNode> filterMediaViewerCandidates() const;

    // The sort half of applyMediaViewerFilter (Architecture.md §14.68): sorts `filtered` by
    // m_mediaViewerSortCriterion/m_mediaViewerSortAscending. Name/Size delegate to
    // FileNavigationUseCase::sortBy; Duration sorts via m_mediaViewerResultsModel's
    // mediaThumbnailDuration() lookup, since duration isn't part of FileNode -- a path whose
    // duration hasn't been decoded yet always sorts after one that has, regardless of ascending/
    // descending.
    std::vector<FileNode> sortMediaViewerResults(std::vector<FileNode> filtered) const;

    // Re-sorts the currently-displayed Media Viewer grid in place (cheap re-order only -- doesn't
    // touch icon/thumbnail requests, since changing sort order never changes which paths need
    // decoding). Used by the three sort/view setters below and by the progressive duration-sort
    // re-order in requestMediaViewerResultThumbnails's batch callback. No-op outside Media Viewer
    // mode.
    void reapplyMediaViewerSort();

    // Dispatches the "whole-kind background sweep" (Architecture.md §14.66) for `kind` -- every
    // candidate matching `kind` alone (not the name filter), decoded at Low priority so it never
    // blocks whatever currently matches the visible filter. No-op if `kind`'s sweep has already
    // been requested this generation (per-generation m_mediaViewerImageSweepRequested/
    // m_mediaViewerVideoSweepRequested, reset in onMediaViewerCandidatesReady). Owns the "Loading
    // Media Viewer" progress notification's determinate phase, sized to just this kind's candidate
    // count -- called once right after a fresh candidate fetch (for m_mediaViewerKind) and again
    // from setMediaViewerKind whenever the dropdown switches to a kind not yet swept this
    // generation, which is what makes that switch actually decode something instead of being a
    // silent cache hit.
    void requestKindSweep(int generation, MediaViewerKind kind);

    // Completion callback for runMediaViewerQuery()'s AdvancedSearchService request. Discards the
    // result if `generation` no longer matches m_mediaViewerGeneration (a newer request, or
    // exitMediaViewer(), superseded it in the meantime) -- the one piece of the generation-counter
    // machinery still needed here for correctness.
    void onMediaViewerCandidatesReady(int generation, Result<std::vector<FileNode>> result);

    FileNavigationUseCase& m_fileNavigationUseCase;
    TagManagementUseCase& m_tagManagementUseCase;
    GitStatusService& m_gitStatusService;
    FolderMonitorUseCase& m_folderMonitorUseCase;
    AdvancedSearchService& m_advancedSearchService;
    FileTagsService& m_fileTagsService;
    DiskUsageService& m_diskUsageService;
    FragmentationService& m_fragmentationService;
    IconLoadingService& m_iconLoadingService;
    MediaThumbnailLoadingService& m_mediaThumbnailLoadingService;
    TagChangeNotifier& m_tagChangeNotifier;
    NotificationCenter& m_notificationCenter;
    NavigationHistory m_history;
    ViewMode m_viewMode = ViewMode::Details;
    // Per-tab column-visibility overrides (Architecture.md §14.49); see TabConfig::hiddenColumns
    // for the tri-state rationale. Default-constructed to all-nullopt, same as TabConfig's.
    std::array<std::optional<bool>, FileListModel::ColumnCount> m_hiddenColumns{};
    FileListModel* m_fileListModel = nullptr;
    std::vector<FileNode> m_selectedEntries;

    FileListModel* m_searchResultsModel = nullptr;
    bool m_searchActive = false;
    std::filesystem::path m_searchRoot;
    std::vector<FileNode> m_searchSnapshot;
    // True only right after a successful recursive scan has fully populated m_searchSnapshot --
    // mirrors m_advancedSearchFullScanDone.
    bool m_searchFullScanDone = false;
    // True from the moment a recursive scan is dispatched until its completion callback runs (with
    // a matching generation) or exitSearch() resets it -- mirrors m_advancedSearchScanPending. While
    // true, a query edit (updateSearchQuery, forceRescan=false) does NOT dispatch a second
    // concurrent scan of the same root; it just re-filters m_searchPartialSnapshot synchronously.
    bool m_searchScanPending = false;
    // Best-known candidate list from the in-flight scan's progress ticks (Architecture.md §14.37),
    // updated by onSearchProgress -- mirrors m_advancedSearchPartialSnapshot.
    std::vector<FileNode> m_searchPartialSnapshot;
    // Tags for whichever candidate list (m_searchSnapshot or m_searchPartialSnapshot) was most
    // recently fetched, fetched via requestSearchResultTags whenever that candidate list itself
    // changes and reapplied as-is by every applySearchFilter re-filter -- a filtered subset never
    // needs a fresh fetch since it's always a subset of the same candidate list.
    TagsByPathMap m_searchSnapshotTags;
    QString m_searchQuery;
    // Match Case/Whole Word/Exact Name/Match Regex toggle state (Architecture.md §14.44), applied
    // by applySearchFilter via FileNavigationUseCase::filterByName's options-aware overload. Reset
    // to their defaults by exitSearch(), mirroring m_searchQuery.
    NameMatchMode m_searchMatchMode = NameMatchMode::Contains;
    bool m_searchCaseSensitive = false;
    // Skip Git-Ignored toggle state (Architecture.md §14.45) -- unlike the three above, changing
    // this forces a rescan (setSearchOptions), since it changes what a scan visits, not just how a
    // candidate list already in memory is filtered. Reset to false by exitSearch().
    bool m_searchSkipGitIgnored = false;
    // Bumped on every recursive-scan dispatch and on exitSearch(); onSearchCandidatesReady/
    // onSearchProgress discard any reply whose generation no longer matches, same discard-by-
    // generation-id approach as m_advancedSearchGeneration above.
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

    // Media Viewer mode (Architecture.md §14.61).
    FileListModel* m_mediaViewerResultsModel = nullptr;
    bool m_mediaViewerActive = false;
    MediaViewerKind m_mediaViewerKind = MediaViewerKind::Image;
    // Sort/view toolbar state (Architecture.md §14.68). Not reset by enterMediaViewer/
    // exitMediaViewer -- unlike m_mediaViewerKind/m_mediaViewerNameQuery, which are tied to the
    // specific root/candidate set a session browses, these are a per-tab UI preference that
    // persists across Media Viewer sessions on the same tab.
    MediaViewerSortCriterion m_mediaViewerSortCriterion = MediaViewerSortCriterion::Name;
    bool m_mediaViewerSortAscending = true;
    MediaViewerViewMode m_mediaViewerViewMode = MediaViewerViewMode::Thumbnails;
    // Tag-click behaviour toggle (Architecture.md §14.72). Not reset by enterMediaViewer/
    // exitMediaViewer, same posture as m_mediaViewerSortCriterion/m_mediaViewerViewMode above.
    MediaViewerTagBehaviour m_mediaViewerTagBehaviour = MediaViewerTagBehaviour::Filter;
    std::filesystem::path m_mediaViewerRoot;
    std::vector<Tag::Id> m_mediaViewerTagIds;
    QString m_mediaViewerNameQuery;
    NameSearchOptions m_mediaViewerNameOptions;
    // Cached candidate set: post-tag-filter (or the whole recursive scan when no tags are
    // applied), pre-kind/name-filter -- re-fetched only when m_mediaViewerTagIds changes;
    // applyMediaViewerFilter() re-filters this in memory for every kind/name-filter edit.
    std::vector<FileNode> m_mediaViewerCandidates;
    // Bumped on every candidate-fetch dispatch and on exitMediaViewer(); discards a stale reply
    // the same way m_advancedSearchGeneration/m_searchGeneration do.
    int m_mediaViewerGeneration = 0;
    // The copy-style progress notification driving entering Media Viewer mode (Architecture.md
    // §14.65) -- 0 means none currently active. Owned/driven directly by this class, same posture
    // FileOperationsController has for copy/move's progress id.
    NotificationCenter::ProgressId m_mediaViewerProgressId = 0;
    std::size_t m_mediaViewerThumbnailsTotal = 0;
    std::size_t m_mediaViewerThumbnailsDone = 0;
    // De-duplicates m_mediaViewerThumbnailsDone's advance (Architecture.md §14.66): a path that
    // the kind's background sweep (Low priority) and the current filter match (High priority) both
    // cover would otherwise have its decode counted twice against the same total. Reset alongside
    // m_mediaViewerThumbnailsTotal/Done whenever a sweep establishes a new total.
    QSet<QString> m_mediaViewerCountedPaths;
    // Per-generation "has this kind's whole-kind background sweep already been dispatched" tracker
    // (Architecture.md §14.66), reset in onMediaViewerCandidatesReady whenever m_mediaViewerCandidates
    // is replaced by a fresh fetch. Guards requestKindSweep against a redundant re-dispatch, and lets
    // setMediaViewerKind tell whether switching to `kind` needs to kick off a real decode (never
    // swept yet) or is a cheap re-filter of already-decoded entries (already swept).
    bool m_mediaViewerImageSweepRequested = false;
    bool m_mediaViewerVideoSweepRequested = false;

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
