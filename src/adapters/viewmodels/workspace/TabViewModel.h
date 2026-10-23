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
#include <QString>
#include <QStringList>

#include "DriveSpaceInfo.h"
#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "FileNode.h"
#include "FolderMonitorUseCase.h"
#include "FolderSizesByPath.h"
#include "GitRepositorySummary.h"
#include "MediaThumbnailLoadingService.h"
#include "NameSearchOptions.h"
#include "NavigationHistory.h"
#include "SearchCriteria.h"
#include "Tag.h"
#include "TabAdvancedSearchViewModel.h"
#include "TabContentSearchViewModel.h"
#include "TabMediaViewerViewModel.h"
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
class ContentSearchService;
class FileTagsService;
class TagChangeNotifier;
class DiskUsageService;
class FragmentationService;
class IconLoadingService;
class NotificationCenter;

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
                 AdvancedSearchService& advancedSearchService, ContentSearchService& contentSearchService,
                 FileTagsService& fileTagsService, DiskUsageService& diskUsageService,
                 FragmentationService& fragmentationService,
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

    // "Find in Files" state (Architecture.md §14.104), extracted into its own owned sub-object
    // (Architecture.md §14.96) -- callers call its methods and connect to its signals directly
    // rather than through TabViewModel forwarders.
    TabContentSearchViewModel* contentSearch() const noexcept { return m_contentSearch; }

    // Media Viewer mode (Architecture.md §14.61), extracted into its own owned sub-object
    // (Architecture.md §14.96) -- callers call its methods and connect to its signals directly
    // rather than through TabViewModel forwarders.
    TabMediaViewerViewModel* mediaViewer() const noexcept { return m_mediaViewer; }

    // Hover preview popup (Architecture.md §14.76): a thin wrapper over
    // MediaThumbnailLoadingService::requestThumbnails for the single hovered `entry`, at
    // Priority::High. Unlike requestBrowsingThumbnails/TabMediaViewerViewModel::requestResultThumbnails, the
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
    // FileBrowserView::selectionChanged. Cleared on every real navigation (see loadAndApply) so a
    // stale selection from the previous folder never leaks into the new one -- but NOT on
    // refresh()'s re-list of the same directory, which reconciles the existing selection instead
    // of wiping it (see reconcileSelectionAfterRefresh).
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

    // Thin wrappers over TabMediaViewerViewModel's own methods of the same name (Architecture.md
    // §14.96), needed for cross-group data a back-reference would otherwise require:
    // enterMediaViewer() exits Quick/Advanced Search first (single-active-mode invariant, same
    // posture as showAdvancedSearchPanel's wrapper) and supplies currentPath() as the fixed
    // recursive root; the other four thread m_selectedEntries through, mirroring
    // applyMediaViewerTagToSelection needing it -- the one cross-group case the original
    // decomposition plan called out explicitly. Every other Media Viewer method
    // (exitMediaViewer/renameMediaViewerEntry/setMediaViewerKind/etc.) needed no such wrapper --
    // call mediaViewer()->exit()/renameEntry()/setKind()/etc. directly.
    void enterMediaViewer();
    void applyMediaViewerTagToSelection(Tag::Id tagId);
    void handleMediaViewerSearchTagActivated(Tag::Id tagId, bool ctrlHeld);
    void acceptMediaViewerSearchTagQuery(const QString& query);
    void createAndApplyMediaViewerTag(const QString& name);

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

    // Quick Search's, Advanced Search's, and Media Viewer's own signals moved to
    // TabQuickSearchViewModel/TabAdvancedSearchViewModel/TabMediaViewerViewModel (Architecture.md
    // §14.96) -- see quickSearch()/advancedSearch()/mediaViewer() accessors.

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
    // `preserveSelection` is true only for refresh()'s own call: a re-list of the SAME directory
    // (DirectoryWatchService's live refresh, or an explicit F5) is not a navigation, so the
    // existing selection shouldn't be wiped the way a real navigation's stale-selection guard
    // requires -- see reconcileSelectionAfterRefresh().
    void loadAndApply(const std::filesystem::path& path, HistoryUpdate historyUpdate, bool preserveSelection = false);
    void emitAvailability();

    // Re-validates m_selectedEntries against a fresh listing of the same directory: paths still
    // present keep their selected status (refreshed to the new FileNode data), anything deleted
    // out from under the selection drops out. Only emits selectedEntriesChanged when the selected
    // *path set* actually changed -- e.g. a live refresh right after the Preview panel's text
    // editor Save (Architecture.md §14.98) must NOT re-signal a selection that, from every
    // consumer's point of view, never changed, or FilePreviewViewModel would tear down and rebuild
    // the in-progress edit session for no reason.
    void reconcileSelectionAfterRefresh(const std::vector<FileNode>& freshEntries);

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

    // Extra Large Icons only (Architecture.md §14.71): mirrors
    // TabMediaViewerViewModel::requestResultThumbnails's shape, but scoped to the normal-browsing
    // model (m_fileListModel) and gated to image/video
    // entries only -- unlike Media Viewer's candidates, which are already media-only by
    // construction, `entries` here is a plain directory listing. No-op if `entries` has no
    // image/video files. Called from the same two places requestIcons(path, entries) already is,
    // both gated to ViewMode::ExtraLargeIcons. Discarded if `path` no longer matches currentPath(),
    // the same staleness guard requestIcons() uses.
    void requestBrowsingThumbnails(const std::filesystem::path& path, const std::vector<FileNode>& entries);

    FileNavigationUseCase& m_fileNavigationUseCase;
    TagManagementUseCase& m_tagManagementUseCase;
    GitStatusService& m_gitStatusService;
    AdvancedSearchService& m_advancedSearchService;
    ContentSearchService& m_contentSearchService;
    FileTagsService& m_fileTagsService;
    DiskUsageService& m_diskUsageService;
    FragmentationService& m_fragmentationService;
    IconLoadingService& m_iconLoadingService;
    MediaThumbnailLoadingService& m_mediaThumbnailLoadingService;
    TagChangeNotifier& m_tagChangeNotifier;
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

    // "Find in Files" state, extracted into its own owned sub-object (Architecture.md §14.96) --
    // exposed via contentSearch() above.
    TabContentSearchViewModel* m_contentSearch;

    // Media Viewer mode (Architecture.md §14.61), extracted into its own owned sub-object
    // (Architecture.md §14.96) -- exposed via mediaViewer() above.
    TabMediaViewerViewModel* m_mediaViewer;

    // Recursive folder monitoring (Architecture.md §14.24), extracted into its own owned sub-object
    // (Architecture.md §14.96) -- exposed via monitoring() above.
    TabMonitoringViewModel* m_monitoring;

    GitRepositorySummary m_lastGitRepositorySummary;
};
