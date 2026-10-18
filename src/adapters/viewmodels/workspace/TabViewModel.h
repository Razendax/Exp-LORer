#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QImage>
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
#include "MediaFolderPreviewData.h"
#include "MediaThumbnailLoadingService.h"
#include "MediaViewerGroupMode.h"
#include "MediaViewerPreviewMode.h"
#include "MediaViewerKind.h"
#include "MediaViewerSortCriterion.h"
#include "MediaViewerTagBehaviour.h"
#include "MediaViewerViewMode.h"
#include "NameSearchOptions.h"
#include "NavigationHistory.h"
#include "NotificationCenter.h"
#include "SearchCriteria.h"
#include "Tag.h"
#include "TabAdvancedSearchViewModel.h"
#include "TabMonitoringViewModel.h"
#include "TabQuickSearchViewModel.h"
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

    // Quick Search ("Search tab") state (Architecture.md §14.42), extracted into its own owned
    // sub-object (Architecture.md §14.96) -- callers call its methods and connect to its signals
    // directly rather than through TabViewModel forwarders.
    TabQuickSearchViewModel* quickSearch() const noexcept { return m_quickSearch; }

    // Advanced (criteria) search pane state (Architecture.md §14.19), extracted into its own owned
    // sub-object (Architecture.md §14.96) -- callers call its methods and connect to its signals
    // directly rather than through TabViewModel forwarders.
    TabAdvancedSearchViewModel* advancedSearch() const noexcept { return m_advancedSearch; }

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

    // Folder-Grouped Browsing (Architecture.md §14.74): a second, root-confined navigation state
    // scoped to Folder mode + Thumbnails view, kept entirely separate from this tab's real
    // currentPath()/m_history so exiting Media Viewer is unaffected. mediaViewerGroupMode() is a
    // toolbar preference like mediaViewerSortCriterion()/mediaViewerViewMode() -- not reset by
    // enter/exitMediaViewer(). mediaViewerCurrentFolder() is the folder currently displayed in
    // Folder mode, equal to m_mediaViewerRoot until Folder-mode sub-navigation moves it elsewhere.
    // Switching the group mode never touches it either way: Recursive mode also scopes its
    // (flat, ungrouped) gallery to mediaViewerCurrentFolder() -- see filterMediaViewerCandidates()
    // -- so switching Folder->Recursive shows a recursive view of wherever Folder-mode browsing
    // currently is, rather than snapping back out to the full original root.
    MediaViewerGroupMode mediaViewerGroupMode() const noexcept { return m_mediaViewerGroupMode; }
    std::filesystem::path mediaViewerCurrentFolder() const { return m_mediaViewerCurrentFolder; }

    // Static/Animate/Autoplay GIF preview mode (Architecture.md §14.83): a toolbar preference like
    // mediaViewerSortCriterion()/mediaViewerViewMode() above -- not reset by enter/exitMediaViewer().
    MediaViewerPreviewMode mediaViewerPreviewMode() const noexcept { return m_mediaViewerPreviewMode; }

    // Pure queries, no history/state side effects -- mirror upTarget()/peekBack()/peekForward()
    // above, but scoped to m_mediaViewerFolderHistory/m_mediaViewerCurrentFolder.
    // mediaViewerUpTarget() clamps at m_mediaViewerRoot the same way upTarget() clamps at
    // VirtualPaths::ThisPC: returning mediaViewerCurrentFolder() itself signals "nothing to
    // navigate to" (already at the root).
    std::filesystem::path mediaViewerUpTarget() const;
    std::optional<std::filesystem::path> mediaViewerPeekBack() const;
    std::optional<std::filesystem::path> mediaViewerPeekForward() const;

    // Resolves mediaViewerTagFilters() into full Tag objects (for chip display), mirroring
    // TabAdvancedSearchViewModel::resolveTagCriteria().
    std::vector<Tag> resolveMediaViewerTagFilters() const;

    // Hover preview popup (Architecture.md §14.76): a thin wrapper over
    // MediaThumbnailLoadingService::requestThumbnails for the single hovered `entry`, at
    // Priority::High. Unlike requestBrowsingThumbnails/requestMediaViewerResultThumbnails, the
    // context passed to the service is `this` rather than m_fileListModel -- a hover request must
    // not purge or be purged by an unrelated in-flight grid-icon request for the same model -- and
    // `entry.path()` itself (not a containing directory) is passed as the service's `root` tag, so
    // its existing per-context staleness check doubles as "superseded by a newer hover target"
    // with no new cancellation plumbing. onReady is invoked with the decoded image once available,
    // unless a newer hover request for a different path has since superseded this one. Does not
    // filter by extension -- the caller (FileBrowserView) already classifies the hover target
    // before calling this.
    void requestHoverPreviewThumbnail(const FileNode& entry, int maxWidth, int maxHeight, std::function<void(QImage)> onReady);

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

    // Delegates to TagManagementUseCase::searchTags; empty vector on repository failure. Shared by
    // Advanced Search's and Media Viewer's own tag-search boxes -- not Advanced-Search-specific,
    // so it stays here rather than moving to TabAdvancedSearchViewModel.
    std::vector<Tag> searchTagsForCriteria(const QString& query) const;

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

    // Recursive folder monitoring (Architecture.md §14.24, "Monitoring panel"), extracted into its
    // own owned sub-object (Architecture.md §14.96) -- callers call its methods and connect to its
    // signals directly rather than through TabViewModel forwarders.
    TabMonitoringViewModel* monitoring() const noexcept { return m_monitoring; }

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

    // Thin wrapper over TabQuickSearchViewModel::updateQuery/refresh (Architecture.md §14.96):
    // Quick Search and Advanced Search stay a single-active-mode invariant, and Advanced Search
    // isn't extracted yet, so exiting it on a real (non-empty) query edit stays TabViewModel's
    // call. Empty/whitespace-only query is left to quickSearch()->updateQuery()/refresh() to turn
    // into an exit, same as before extraction.
    void updateSearchQuery(const QString& query);
    void refreshSearch(const QString& query);

    // Thin wrappers over TabAdvancedSearchViewModel::showPanel/start/addTagCriterion (Architecture.
    // md §14.96): each unconditionally exits Quick Search first (single-active-mode invariant) --
    // always safe even when Quick Search is already inactive, since
    // TabQuickSearchViewModel::exit() no-ops in that case, same posture as updateSearchQuery()'s
    // wrapper above. updateAdvancedSearchCriteria()/exitAdvancedSearch()/removeTagSearchCriterion()
    // need no such wrapper -- call advancedSearch()->updateCriteria()/exit()/removeTagCriterion()
    // directly.
    void showAdvancedSearchPanel();
    void startAdvancedSearch(const SearchCriteria& criteria);
    void addTagSearchCriterion(Tag::Id tagId);

    // Media Viewer mode (Architecture.md §14.61): a focused, full-pane recursive gallery of every
    // image/video under currentPath(), entered from the View menu. No-op if already active.
    // Captures currentPath() as the fixed recursive root, resets kind/filters to their defaults,
    // exits Quick/Advanced Search first (single-active-mode invariant, same posture as
    // showAdvancedSearchPanel), dispatches the initial candidate fetch, and emits
    // mediaViewerModeChanged(true).
    void enterMediaViewer();

    // Clears Media Viewer state and flips mediaViewerActive off. No-op if already inactive.
    void exitMediaViewer();

    // Called by WorkspaceController::onPathRenamed for every tab, regardless of focus or
    // currentPath(): Media Viewer's results model is a cached snapshot (§14.61) that a directory
    // re-list never touches (unlike normal browsing's m_fileListModel), so without this a rename
    // committed from the grid left the tile showing the old name until the tab exited and
    // re-entered the mode. No-op outside Media Viewer mode or if `source` isn't one of the cached
    // candidates (or their containing folder, for a renamed ancestor directory). Rebases every
    // affected candidate's path via PathContainment.h's rebasePath and re-filters in memory only
    // (applyMediaViewerFilter(false)) -- never a rescan.
    void renameMediaViewerEntry(const std::filesystem::path& source, const std::filesystem::path& destination);

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

    // Enter-key commit for the Media Viewer tag-search field: assigns the existing exact-name match
    // (re-queried via TagManagementUseCase::searchTags, same as the chip row) to the current
    // selection if there is one, otherwise creates the tag and assigns it. No-op on an empty query
    // or outside Media Viewer mode. Unlike handleMediaViewerSearchTagActivated, never filters -- this
    // always applies, independent of mediaViewerTagBehaviour().
    void acceptMediaViewerSearchTagQuery(const QString& query);

    // Creates a new tag named `name` (color auto-picked via TagColorPalette::pickColorFor, same as
    // TagListViewModel::createAndAddTagFromQuery) and assigns it to the current selection via
    // applyMediaViewerTagToSelection. No-op on an empty name or outside Media Viewer mode; emits
    // mediaViewerFailed on a TagManagementUseCase::createTag failure.
    void createAndApplyMediaViewerTag(const QString& name);

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

    // Folder-Grouped Browsing's own group-mode toggle (Architecture.md §14.74). No-op if unchanged
    // or outside Media Viewer mode. Neither direction touches mediaViewerCurrentFolder()/the
    // folder-nav history -- switching Recursive->ByFolder resumes Folder-mode browsing wherever it
    // last was (m_mediaViewerRoot the first time, since nothing has navigated it yet), and
    // switching ByFolder->Recursive keeps the recursive gallery scoped to that same folder (see
    // filterMediaViewerCandidates()), rather than either direction snapping back out to the full
    // root. Re-applies the current filter so the grid reflects the new grouping immediately.
    void setMediaViewerGroupMode(MediaViewerGroupMode mode);

    // Static/Animate/Autoplay GIF preview mode toggle (Architecture.md §14.83). No-op if unchanged
    // or outside Media Viewer mode, same guard as setMediaViewerGroupMode.
    void setMediaViewerPreviewMode(MediaViewerPreviewMode mode);

    // Folder-Grouped Browsing's sub-navigation (Architecture.md §14.74) -- exact mirror of
    // navigateTo()/goUp()/goBack()/goForward(), but confined to m_mediaViewerRoot's subtree and
    // never touching disk/Ports: these just rebuild the already-cached candidate list for the new
    // current folder. No-op outside Media Viewer mode. mediaViewerNavigateTo() additionally no-ops
    // (silently rejects) a path outside m_mediaViewerRoot's subtree -- the one piece of real
    // validation here, done via PathContainment.h rather than a disk round-trip.
    void mediaViewerNavigateTo(const std::filesystem::path& path);
    void mediaViewerGoUp();
    void mediaViewerGoBack();
    void mediaViewerGoForward();

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

    // Quick Search's and Advanced Search's own signals moved to TabQuickSearchViewModel/
    // TabAdvancedSearchViewModel (Architecture.md §14.96) -- see quickSearch()/advancedSearch()
    // accessors.

    // Media Viewer mode (Architecture.md §14.61).
    void mediaViewerModeChanged(bool active);
    void mediaViewerResultsChanged(const std::vector<FileNode>& entries);
    void mediaViewerKindChanged(MediaViewerKind kind);
    // Lets the UI re-sync its applied-tag-filter chip row regardless of which entry point changed
    // the filter set (mirrors TabAdvancedSearchViewModel::criteriaChanged).
    void mediaViewerCriteriaChanged();
    // Media Viewer mode's toolbar sort/view controls (Architecture.md §14.68).
    void mediaViewerSortCriterionChanged(MediaViewerSortCriterion criterion);
    void mediaViewerSortAscendingChanged(bool ascending);
    void mediaViewerViewModeChanged(MediaViewerViewMode mode);
    void mediaViewerTagBehaviourChanged(MediaViewerTagBehaviour behaviour);
    // Folder-Grouped Browsing (Architecture.md §14.74).
    void mediaViewerGroupModeChanged(MediaViewerGroupMode mode);
    // Static/Animate/Autoplay GIF preview mode (Architecture.md §14.83).
    void mediaViewerPreviewModeChanged(MediaViewerPreviewMode mode);
    void mediaViewerCurrentFolderChanged(const std::filesystem::path& folder);
    void mediaViewerBackAvailableChanged(bool available);
    void mediaViewerForwardAvailableChanged(bool available);
    void mediaViewerUpAvailableChanged(bool available);
    void mediaViewerLoading();
    void mediaViewerLoadingFinished();
    void mediaViewerFailed(const QString& message);

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

    // Folder-Grouped Browsing's own availability-signal emit (Architecture.md §14.74), mirroring
    // emitAvailability() above -- shared tail for mediaViewerNavigateTo/mediaViewerGoUp/
    // mediaViewerGoBack/mediaViewerGoForward/setMediaViewerGroupMode's into-ByFolder branch.
    void emitMediaViewerFolderAvailability();

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

    // Same as requestIcons, for the Media Viewer results model -- called once per candidate-set
    // fetch (onMediaViewerCandidatesReady), not on every in-memory kind/name filter edit (same
    // "icons only follow a real fetch" posture TabAdvancedSearchViewModel::requestResultIcons
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

    // Same shape as TabQuickSearchViewModel::requestResultTags, for the Media Viewer results model
    // (Architecture.md §14.69's Details view tag row): fetches tags for
    // `candidates`, discarded if `generation` no longer matches m_mediaViewerGeneration, then
    // applies directly to m_mediaViewerResultsModel. Called from applyMediaViewerFilter() alongside
    // the existing icon/thumbnail requests, and from the TagChangeNotifier::tagsChanged handler.
    // Unlike the search/advanced-search siblings, no separate snapshot-tags cache is kept -- Media
    // Viewer's own candidate list already survives in-memory kind/name re-filters without needing
    // a fresh tag fetch (refilterEntries() leaves setFileTags()'s prior result alone).
    void requestMediaViewerResultTags(int generation, const std::vector<FileNode>& candidates);

    // Quick Search's and Advanced Search's own runSearch/applyFilter/onCandidatesReady/onProgress
    // moved to TabQuickSearchViewModel/TabAdvancedSearchViewModel (Architecture.md §14.96) -- see
    // quickSearch()/advancedSearch() accessors.

    // Dispatches Media Viewer mode's candidate fetch via the already-injected
    // m_advancedSearchService (Architecture.md §14.61) -- tag-first (requestTagFirstCandidates)
    // when m_mediaViewerTagIds is non-empty, else a plain recursive scan
    // (requestRecursiveScan) of m_mediaViewerRoot. Deliberately simpler than
    // TabAdvancedSearchViewModel::run(): no in-flight-scan reuse or progress-tick machinery, since
    // this is only ever called right after the tag filter set itself changed (enterMediaViewer/
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

    // Folder-Grouped Browsing's own regroup step (Architecture.md §14.74): buckets `filtered`
    // (already kind+name filtered, all under m_mediaViewerRoot per filterMediaViewerCandidates())
    // into mediaViewerCurrentFolder()'s direct-child files (kept as-is) plus one synthesized
    // FileType::Directory FileNode per non-empty immediate-subfolder bucket -- recursive, i.e.
    // every qualifying file nested anywhere below that subfolder, not just its direct children.
    struct FolderGroupedView
    {
        // Direct-child files + synthesized subfolder FileNodes, unsorted -- the caller
        // (buildMediaViewerDisplay()) runs this through sortMediaViewerResults() itself, same as
        // the flat (non-grouped) path does.
        std::vector<FileNode> combined;
        // Just the direct-child files -- these need their own icon/thumbnail/tag requests, same as
        // the flat (non-grouped) path.
        std::vector<FileNode> directChildFiles;
        // The (up to 4 per bucket) sample files backing previewsByPath's seeds -- a folder tile's
        // collage is drawn entirely from these once decoded (see FolderPreviewRole), so they need
        // their own thumbnail request too, same as directChildFiles, rather than waiting on
        // requestKindSweep's whole-tree Low-priority background sweep to eventually reach them
        // (Architecture.md §14.74).
        std::vector<FileNode> previewSampleFiles;
        // One seed per synthesized subfolder row, keyed by its absolute path -- itemCount plus up
        // to 4 sample paths (in the current sort order) for FileListModel::setFolderPreviews().
        QHash<QString, FolderPreviewSeed> previewsByPath;
    };
    FolderGroupedView buildFolderGroupedView(const std::vector<FileNode>& filtered) const;

    // The full "what should the grid show right now" computation shared by applyMediaViewerFilter()
    // and reapplyMediaViewerSort() (Architecture.md §14.74): filters, then -- when Folder mode +
    // Thumbnails view are both active -- regroups via buildFolderGroupedView(), then sorts. Kept as
    // one shared helper (rather than inlined into both callers) specifically so reapplyMediaViewerSort()
    // can pick up a regroup/re-sort without re-dispatching the icon/thumbnail/tag requests
    // applyMediaViewerFilter() issues for its `dispatchTargets` -- a plain re-sort never changes
    // which paths need decoding.
    struct MediaViewerDisplay
    {
        // Sorted, display-ready list -- includes synthesized subfolder FileNodes when grouped.
        std::vector<FileNode> entries;
        // What needs icon/thumbnail/tag requests: equals entries itself when not grouped, or just
        // the direct-child files when grouped.
        std::vector<FileNode> dispatchTargets;
        // Only non-empty when grouped.
        QHash<QString, FolderPreviewSeed> folderPreviews;
        bool grouped = false;
    };
    MediaViewerDisplay buildMediaViewerDisplay() const;

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

    // Quick Search state, extracted into its own owned sub-object (Architecture.md §14.96) --
    // exposed via quickSearch() above.
    TabQuickSearchViewModel* m_quickSearch;

    // Advanced (criteria) search pane state, extracted into its own owned sub-object (Architecture.
    // md §14.96) -- exposed via advancedSearch() above.
    TabAdvancedSearchViewModel* m_advancedSearch;

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
    // Static/Animate/Autoplay GIF preview mode toggle (Architecture.md §14.83). Not reset by
    // enterMediaViewer/exitMediaViewer, same posture as m_mediaViewerTagBehaviour above.
    MediaViewerPreviewMode m_mediaViewerPreviewMode = MediaViewerPreviewMode::Static;
    // Folder-Grouped Browsing (Architecture.md §14.74). m_mediaViewerGroupMode is a toolbar
    // preference like m_mediaViewerSortCriterion/m_mediaViewerViewMode above -- not reset by
    // enterMediaViewer/exitMediaViewer. m_mediaViewerCurrentFolder/m_mediaViewerFolderHistory are a
    // second, root-confined navigation state scoped to Folder-mode sub-browsing, reset to
    // m_mediaViewerRoot by enterMediaViewer() and whenever setMediaViewerGroupMode() switches into
    // ByFolder -- kept entirely separate from m_history/currentPath() so exiting Media Viewer is
    // unaffected.
    MediaViewerGroupMode m_mediaViewerGroupMode = MediaViewerGroupMode::Recursive;
    std::filesystem::path m_mediaViewerCurrentFolder;
    NavigationHistory m_mediaViewerFolderHistory;
    std::filesystem::path m_mediaViewerRoot;
    std::vector<Tag::Id> m_mediaViewerTagIds;
    QString m_mediaViewerNameQuery;
    NameSearchOptions m_mediaViewerNameOptions;
    // Cached candidate set: post-tag-filter (or the whole recursive scan when no tags are
    // applied), pre-kind/name-filter -- re-fetched only when m_mediaViewerTagIds changes;
    // applyMediaViewerFilter() re-filters this in memory for every kind/name-filter edit.
    std::vector<FileNode> m_mediaViewerCandidates;
    // Bumped on every candidate-fetch dispatch and on exitMediaViewer(); discards a stale reply
    // the same way TabQuickSearchViewModel's and TabAdvancedSearchViewModel's own generation
    // counters do for their own replies.
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
    bool m_mediaViewerAnimationSweepRequested = false;

    // Recursive folder monitoring (Architecture.md §14.24), extracted into its own owned sub-object
    // (Architecture.md §14.96) -- exposed via monitoring() above.
    TabMonitoringViewModel* m_monitoring;

    GitRepositorySummary m_lastGitRepositorySummary;
};
