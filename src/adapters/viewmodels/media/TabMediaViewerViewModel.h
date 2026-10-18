#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "FileListModel.h"
#include "MediaFolderPreviewData.h"
#include "MediaThumbnailLoadingService.h"
#include "MediaViewerGroupMode.h"
#include "MediaViewerKind.h"
#include "MediaViewerPreviewMode.h"
#include "MediaViewerSortCriterion.h"
#include "MediaViewerTagBehaviour.h"
#include "MediaViewerViewMode.h"
#include "NameSearchOptions.h"
#include "NavigationHistory.h"
#include "NotificationCenter.h"
#include "Result.h"
#include "Tag.h"

class AdvancedSearchService;
class FileDecorationRules;
class FileTagsService;
class IconLoadingService;
class TagChangeNotifier;
class TagManagementUseCase;

// Media Viewer mode (Architecture.md §14.61): a focused, full-pane recursive gallery of every
// image/video under whatever folder was current when it was entered, extracted out of
// TabViewModel (Architecture.md §14.96) -- by far the largest and most internally tangled group
// (gallery mode, its own tagging, Folder-Grouped Browsing, GIF preview mode all share one
// generation counter/candidate cache). Owned by TabViewModel via `this`-parenting, exposed
// through TabViewModel::mediaViewer(); callers call its slots and connect to its signals directly
// rather than through TabViewModel forwarders.
//
// Cross-group dependencies stay thin TabViewModel wrappers/ctor wiring rather than a back-
// reference to the owner (Architecture.md §14.96): enter() takes `currentPath` as a parameter
// (captured as the fixed recursive root) since navigation state isn't part of this sub-object;
// TabViewModel::enterMediaViewer() exits Quick/Advanced Search first (single-active-mode
// invariant) before calling enter(); selection-clearing on activate/deactivate is wired via
// modeChanged in TabViewModel's constructor; and applyTagToSelection/handleSearchTagActivated/
// acceptSearchTagQuery/createAndApplyTag all take `selectedEntries` as a parameter, threaded
// through from thin TabViewModel wrappers of the same name (mirrors applyMediaViewerTagToSelection
// needing m_selectedEntries, the one cross-group case the original decomposition plan called out
// explicitly).
class TabMediaViewerViewModel : public QObject
{
    Q_OBJECT

public:
    TabMediaViewerViewModel(AdvancedSearchService& advancedSearchService, TagManagementUseCase& tagManagementUseCase,
                             FileTagsService& fileTagsService, IconLoadingService& iconLoadingService,
                             MediaThumbnailLoadingService& mediaThumbnailLoadingService, TagChangeNotifier& tagChangeNotifier,
                             NotificationCenter& notificationCenter, const FileDecorationRules& fileDecorationRules,
                             QObject& fileDecorationsChangeSource, QObject* parent = nullptr);
    ~TabMediaViewerViewModel() override;

    FileListModel* resultsModel() const noexcept { return m_resultsModel; }
    bool isActive() const noexcept { return m_active; }
    MediaViewerKind kind() const noexcept { return m_kind; }
    const std::vector<Tag::Id>& tagFilters() const noexcept { return m_tagIds; }
    QString nameFilter() const noexcept { return m_nameQuery; }
    NameSearchOptions nameOptions() const noexcept { return m_nameOptions; }
    MediaViewerSortCriterion sortCriterion() const noexcept { return m_sortCriterion; }
    bool sortAscending() const noexcept { return m_sortAscending; }
    MediaViewerViewMode viewMode() const noexcept { return m_viewMode; }
    MediaViewerTagBehaviour tagBehaviour() const noexcept { return m_tagBehaviour; }

    // Folder-Grouped Browsing (Architecture.md §14.74): a second, root-confined navigation state
    // scoped to Folder mode + Thumbnails view, kept entirely separate from the tab's real
    // currentPath()/history so exiting Media Viewer is unaffected. groupMode() is a toolbar
    // preference like sortCriterion()/viewMode() -- not reset by enter()/exit(). currentFolder()
    // is the folder currently displayed in Folder mode, equal to the recursive root until
    // Folder-mode sub-navigation moves it elsewhere. Switching the group mode never touches it
    // either way: Recursive mode also scopes its (flat, ungrouped) gallery to currentFolder() --
    // see filterCandidates() -- so switching Folder->Recursive shows a recursive view of wherever
    // Folder-mode browsing currently is, rather than snapping back out to the full original root.
    MediaViewerGroupMode groupMode() const noexcept { return m_groupMode; }
    std::filesystem::path currentFolder() const { return m_currentFolder; }

    // Static/Animate/Autoplay GIF preview mode (Architecture.md §14.83): a toolbar preference like
    // sortCriterion()/viewMode() above -- not reset by enter()/exit().
    MediaViewerPreviewMode previewMode() const noexcept { return m_previewMode; }

    // Pure queries, no history/state side effects -- mirror TabViewModel::upTarget()/peekBack()/
    // peekForward(), but scoped to m_folderHistory/m_currentFolder. upTarget() clamps at the
    // recursive root the same way TabViewModel::upTarget() clamps at VirtualPaths::ThisPC:
    // returning currentFolder() itself signals "nothing to navigate to" (already at the root).
    std::filesystem::path upTarget() const;
    std::optional<std::filesystem::path> peekBack() const;
    std::optional<std::filesystem::path> peekForward() const;

    // Resolves tagFilters() into full Tag objects (for chip display), mirroring
    // TabAdvancedSearchViewModel::resolveTagCriteria().
    std::vector<Tag> resolveTagFilters() const;

public slots:
    // A focused, full-pane recursive gallery of every image/video under `currentPath`, entered
    // from the View menu. No-op if already active. Captures `currentPath` as the fixed recursive
    // root, resets kind/filters to their defaults, dispatches the initial candidate fetch, and
    // emits modeChanged(true). The single-active-mode invariant with Quick/Advanced Search is
    // TabViewModel::enterMediaViewer()'s responsibility (it exits them first, then calls this).
    void enter(const std::filesystem::path& currentPath);

    // Clears Media Viewer state and flips isActive() off. No-op if already inactive.
    void exit();

    // Called by WorkspaceController::onPathRenamed for every tab, regardless of focus or
    // currentPath(): the results model is a cached snapshot (§14.61) that a directory re-list
    // never touches (unlike normal browsing's model), so without this a rename committed from the
    // grid left the tile showing the old name until the tab exited and re-entered the mode. No-op
    // outside Media Viewer mode or if `source` isn't one of the cached candidates (or their
    // containing folder, for a renamed ancestor directory). Rebases every affected candidate's
    // path via PathContainment.h's rebasePath and re-filters in memory only (applyFilter(false))
    // -- never a rescan.
    void renameEntry(const std::filesystem::path& source, const std::filesystem::path& destination);

    // Switches the grid between images and videos -- re-filters the cached candidate set in
    // memory only (no rescan: the cached set is already post-tag-filter, pre-kind/name-filter).
    // No-op if already active or outside Media Viewer mode.
    void setKind(MediaViewerKind kind);

    // Adds/removes a tag from the AND-combined tag filter (no-op if already present/absent) and
    // re-dispatches the candidate fetch, since the tag set determines which candidates are even
    // fetched (tag-first vs. recursive-scan branch of run()). No-op outside Media Viewer mode.
    void addTagFilter(Tag::Id tagId);
    void removeTagFilter(Tag::Id tagId);

    // Media Viewer mode's tag behaviour toggle (Architecture.md §14.72): flips what a plain click
    // vs. a Ctrl+click on a tag chip/pill does in the slots below. Not reset by enter()/exit() --
    // a per-tab UI preference, same posture as m_sortCriterion/m_viewMode.
    void setTagBehaviour(MediaViewerTagBehaviour behaviour);

    // The search-results tag-chip click (MediaViewerSearchPanelWidget's matching-tag row): XOR's
    // ctrlHeld against the current behaviour to decide between adding tagId to the filter set and
    // applying it to `selectedEntries`. No-op outside Media Viewer mode.
    void handleSearchTagActivated(Tag::Id tagId, bool ctrlHeld, const std::vector<FileNode>& selectedEntries);

    // Assigns tagId to every entry in `selectedEntries` (ignoring a per-entry AlreadyExists),
    // emitting failed if the selection is empty and a "Tag Applied" popup + tagsChanged on at
    // least one success. Used by handleSearchTagActivated's "Apply" branch.
    void applyTagToSelection(Tag::Id tagId, const std::vector<FileNode>& selectedEntries);

    // Enter-key commit for the Media Viewer tag-search field: assigns the existing exact-name
    // match (re-queried via TagManagementUseCase::searchTags, same as the chip row) to
    // `selectedEntries` if there is one, otherwise creates the tag and assigns it. No-op on an
    // empty query or outside Media Viewer mode. Unlike handleSearchTagActivated, never filters --
    // this always applies, independent of tagBehaviour().
    void acceptSearchTagQuery(const QString& query, const std::vector<FileNode>& selectedEntries);

    // Creates a new tag named `name` (color auto-picked via TagColorPalette::pickColorFor, same as
    // TagListViewModel::createAndAddTagFromQuery) and assigns it to `selectedEntries` via
    // applyTagToSelection. No-op on an empty name or outside Media Viewer mode; emits failed on a
    // TagManagementUseCase::createTag failure.
    void createAndApplyTag(const QString& name, const std::vector<FileNode>& selectedEntries);

    // The Details-view per-item tag-pill click: same XOR rule as above, but the "other" action is
    // removing tagId from `entry` specifically rather than applying it to a selection. No-op
    // outside Media Viewer mode. Takes `entry` directly, so unlike the slots above this needs no
    // selectedEntries parameter.
    void handleDetailsTagClick(const FileNode& entry, Tag::Id tagId, bool ctrlHeld);

    // Updates the name/Match Case/Whole Word/Exact Name/Match Regex filter and re-filters the
    // cached candidate set in memory only -- never dispatches a rescan, since neither affects
    // which candidates were fetched. No-op outside Media Viewer mode.
    void setNameFilter(const QString& query, const NameSearchOptions& options);

    // Media Viewer mode's sort-by/direction/view controls (Architecture.md §14.68). Each is a
    // no-op if unchanged, else updates the member, emits its signal, and re-sorts the already-
    // fetched candidate set in memory only (reapplySort()) -- never dispatches a rescan, same
    // posture as setNameFilter.
    void setSortCriterion(MediaViewerSortCriterion criterion);
    void setSortAscending(bool ascending);

    // Stores the chosen view and emits the signal; has no behavioral effect yet (the grid stays
    // locked to DisplayMode::MediaViewerGrid either way) -- deliberately stubbed "one option
    // today, more later" per the request.
    void setViewMode(MediaViewerViewMode mode);

    // Folder-Grouped Browsing's own group-mode toggle (Architecture.md §14.74). No-op if unchanged
    // or outside Media Viewer mode. Neither direction touches currentFolder()/the folder-nav
    // history -- switching Recursive->ByFolder resumes Folder-mode browsing wherever it last was
    // (the recursive root the first time, since nothing has navigated it yet), and switching
    // ByFolder->Recursive keeps the recursive gallery scoped to that same folder (see
    // filterCandidates()), rather than either direction snapping back out to the full root.
    // Re-applies the current filter so the grid reflects the new grouping immediately.
    void setGroupMode(MediaViewerGroupMode mode);

    // Static/Animate/Autoplay GIF preview mode toggle (Architecture.md §14.83). No-op if unchanged
    // or outside Media Viewer mode, same guard as setGroupMode.
    void setPreviewMode(MediaViewerPreviewMode mode);

    // Folder-Grouped Browsing's sub-navigation (Architecture.md §14.74) -- exact mirror of
    // TabViewModel::navigateTo()/goUp()/goBack()/goForward(), but confined to the recursive
    // root's subtree and never touching disk/Ports: these just rebuild the already-cached
    // candidate list for the new current folder. No-op outside Media Viewer mode. navigateTo()
    // additionally no-ops (silently rejects) a path outside the recursive root's subtree -- the
    // one piece of real validation here, done via PathContainment.h rather than a disk round-trip.
    void navigateTo(const std::filesystem::path& path);
    void goUp();
    void goBack();
    void goForward();

signals:
    void modeChanged(bool active);
    void resultsChanged(const std::vector<FileNode>& entries);
    void kindChanged(MediaViewerKind kind);
    // Lets the UI re-sync its applied-tag-filter chip row regardless of which entry point changed
    // the filter set (mirrors TabAdvancedSearchViewModel::criteriaChanged).
    void criteriaChanged();
    // Media Viewer mode's toolbar sort/view controls (Architecture.md §14.68).
    void sortCriterionChanged(MediaViewerSortCriterion criterion);
    void sortAscendingChanged(bool ascending);
    void viewModeChanged(MediaViewerViewMode mode);
    void tagBehaviourChanged(MediaViewerTagBehaviour behaviour);
    // Folder-Grouped Browsing (Architecture.md §14.74).
    void groupModeChanged(MediaViewerGroupMode mode);
    // Static/Animate/Autoplay GIF preview mode (Architecture.md §14.83).
    void previewModeChanged(MediaViewerPreviewMode mode);
    void currentFolderChanged(const std::filesystem::path& folder);
    void backAvailableChanged(bool available);
    void forwardAvailableChanged(bool available);
    void upAvailableChanged(bool available);
    void loading();
    void loadingFinished();
    void failed(const QString& message);

private:
    // Folder-Grouped Browsing's own availability-signal emit (Architecture.md §14.74), mirroring
    // TabViewModel::emitAvailability() -- shared tail for navigateTo/goUp/goBack/goForward/
    // setGroupMode's into-ByFolder branch.
    void emitFolderAvailability();

    // Dispatches Media Viewer mode's candidate fetch via the already-injected
    // m_advancedSearchService (Architecture.md §14.61) -- tag-first (requestTagFirstCandidates)
    // when m_tagIds is non-empty, else a plain recursive scan (requestRecursiveScan) of m_root.
    // Deliberately simpler than TabAdvancedSearchViewModel::run(): no in-flight-scan reuse or
    // progress-tick machinery, since this is only ever called right after the tag filter set
    // itself changed (enter()/addTagFilter()/removeTagFilter()), not on every keystroke. On
    // completion, caches the result into m_candidates and calls applyFilter().
    void run();

    // Filters m_candidates by MediaExtensions::isImageExtension/isVideoExtension for m_kind, then
    // by m_nameQuery/m_nameOptions (reusing FileNavigationUseCase::filterByName), sorts, and
    // applies to m_resultsModel. The tail shared by onCandidatesReady and every in-memory
    // kind/name-filter edit. `freshCandidates` selects which FileListModel method applies the
    // filtered result: onCandidatesReady (just replaced m_candidates from a new scan) passes
    // true, going through setEntries() to drop stale per-path caches from the previous candidate
    // set; a plain in-memory kind/name-filter edit of the SAME candidates passes false, going
    // through refilterEntries() so already-resolved icons/thumbnails survive narrowing and
    // widening the filter without flickering back to their placeholder (Architecture.md §14.62).
    // The kind+name filtering step itself is factored out into filterCandidates() and the sort
    // step into sortResults() (Architecture.md §14.68) so reapplySort() can re-sort without
    // re-filtering.
    void applyFilter(bool freshCandidates);

    // The kind+name filtering half of applyFilter (Architecture.md §14.68): filters m_candidates
    // by MediaExtensions::isImageExtension/isVideoExtension for m_kind, then by m_nameQuery/
    // m_nameOptions.
    std::vector<FileNode> filterCandidates() const;

    // The sort half of applyFilter (Architecture.md §14.68): sorts `filtered` by
    // m_sortCriterion/m_sortAscending. Name/Size delegate to FileNavigationUseCase::sortBy;
    // Duration sorts via m_resultsModel's mediaThumbnailDuration() lookup, since duration isn't
    // part of FileNode -- a path whose duration hasn't been decoded yet always sorts after one
    // that has, regardless of ascending/descending.
    std::vector<FileNode> sortResults(std::vector<FileNode> filtered) const;

    // Folder-Grouped Browsing's own regroup step (Architecture.md §14.74): buckets `filtered`
    // (already kind+name filtered, all under m_root per filterCandidates()) into
    // currentFolder()'s direct-child files (kept as-is) plus one synthesized FileType::Directory
    // FileNode per non-empty immediate-subfolder bucket -- recursive, i.e. every qualifying file
    // nested anywhere below that subfolder, not just its direct children.
    struct FolderGroupedView
    {
        // Direct-child files + synthesized subfolder FileNodes, unsorted -- the caller
        // (buildDisplay()) runs this through sortResults() itself, same as the flat (non-grouped)
        // path does.
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

    // The full "what should the grid show right now" computation shared by applyFilter() and
    // reapplySort() (Architecture.md §14.74): filters, then -- when Folder mode + Thumbnails view
    // are both active -- regroups via buildFolderGroupedView(), then sorts. Kept as one shared
    // helper (rather than inlined into both callers) specifically so reapplySort() can pick up a
    // regroup/re-sort without re-dispatching the icon/thumbnail/tag requests applyFilter() issues
    // for its `dispatchTargets` -- a plain re-sort never changes which paths need decoding.
    struct Display
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
    Display buildDisplay() const;

    // Re-sorts the currently-displayed Media Viewer grid in place (cheap re-order only -- doesn't
    // touch icon/thumbnail requests, since changing sort order never changes which paths need
    // decoding). Used by the three sort/view setters and by the progressive duration-sort re-order
    // in requestResultThumbnails's batch callback. No-op outside Media Viewer mode.
    void reapplySort();

    // Dispatches the "whole-kind background sweep" (Architecture.md §14.66) for `kind` -- every
    // candidate matching `kind` alone (not the name filter), decoded at Low priority so it never
    // blocks whatever currently matches the visible filter. No-op if `kind`'s sweep has already
    // been requested this generation (per-generation m_imageSweepRequested/m_videoSweepRequested,
    // reset in onCandidatesReady). Owns the "Loading Media Viewer" progress notification's
    // determinate phase, sized to just this kind's candidate count -- called once right after a
    // fresh candidate fetch (for m_kind) and again from setKind whenever the dropdown switches to
    // a kind not yet swept this generation, which is what makes that switch actually decode
    // something instead of being a silent cache hit.
    void requestKindSweep(int generation, MediaViewerKind kind);

    // Completion callback for run()'s AdvancedSearchService request. Discards the result if
    // `generation` no longer matches m_generation (a newer request, or exit(), superseded it in
    // the meantime) -- the one piece of the generation-counter machinery still needed here for
    // correctness.
    void onCandidatesReady(int generation, Result<std::vector<FileNode>> result);

    // Same as TabViewModel::requestIcons, for this sub-object's own results model -- called once
    // per candidate-set fetch (onCandidatesReady), not on every in-memory kind/name filter edit
    // (same "icons only follow a real fetch" posture TabAdvancedSearchViewModel::requestResultIcons
    // already has). Discarded if `generation` no longer matches m_generation.
    void requestResultIcons(int generation, const std::vector<FileNode>& candidates);

    // Same as requestResultIcons, for the grid's real decoded thumbnails (Architecture.md §14.62)
    // rather than the plain DecorationRole icon -- called once per candidate-set fetch
    // (onCandidatesReady), same "only follows a real fetch" posture. Discarded if `generation` no
    // longer matches m_generation.
    //
    // Every delivered batch advances the Loading Media Viewer progress notification's determinate
    // phase (Architecture.md §14.66), whenever one is active (m_progressId != 0) -- not just
    // requestKindSweep's own Low-priority sweep dispatch. applyFilter's High-priority call (the
    // kind+name-filtered subset, issued first so it jumps the queue ahead of an in-flight sweep)
    // counts too: with no active name filter that subset is most or all of the kind, so leaving it
    // uncounted left the bar stuck at 0 while the grid was already filling in (see "Progress-
    // counting correctness" in the decision log). Counting is de-duplicated by path
    // (m_countedPaths) so the same file decoded by both the High and Low dispatch is only ever
    // counted once. `priority` is forwarded as-is to MediaThumbnailLoadingService::
    // requestThumbnails.
    void requestResultThumbnails(int generation, const std::vector<FileNode>& candidates,
                                  MediaThumbnailLoadingService::Priority priority);

    // Same shape as TabQuickSearchViewModel::requestResultTags, for this sub-object's own results
    // model (Architecture.md §14.69's Details view tag row): fetches tags for `candidates`,
    // discarded if `generation` no longer matches m_generation, then applies directly to
    // m_resultsModel. Called from applyFilter() alongside the existing icon/thumbnail requests,
    // and from the TagChangeNotifier::tagsChanged handler. Unlike the search/advanced-search
    // siblings, no separate snapshot-tags cache is kept -- the candidate list already survives
    // in-memory kind/name re-filters without needing a fresh tag fetch (refilterEntries() leaves
    // setFileTags()'s prior result alone).
    void requestResultTags(int generation, const std::vector<FileNode>& candidates);

    AdvancedSearchService& m_advancedSearchService;
    TagManagementUseCase& m_tagManagementUseCase;
    FileTagsService& m_fileTagsService;
    IconLoadingService& m_iconLoadingService;
    MediaThumbnailLoadingService& m_mediaThumbnailLoadingService;
    TagChangeNotifier& m_tagChangeNotifier;
    NotificationCenter& m_notificationCenter;

    FileListModel* m_resultsModel = nullptr;
    bool m_active = false;
    MediaViewerKind m_kind = MediaViewerKind::Image;
    // Sort/view toolbar state (Architecture.md §14.68). Not reset by enter()/exit() -- unlike
    // m_kind/m_nameQuery, which are tied to the specific root/candidate set a session browses,
    // these are a per-tab UI preference that persists across Media Viewer sessions on the same
    // tab.
    MediaViewerSortCriterion m_sortCriterion = MediaViewerSortCriterion::Name;
    bool m_sortAscending = true;
    MediaViewerViewMode m_viewMode = MediaViewerViewMode::Thumbnails;
    // Tag-click behaviour toggle (Architecture.md §14.72). Not reset by enter()/exit(), same
    // posture as m_sortCriterion/m_viewMode above.
    MediaViewerTagBehaviour m_tagBehaviour = MediaViewerTagBehaviour::Filter;
    // Static/Animate/Autoplay GIF preview mode toggle (Architecture.md §14.83). Not reset by
    // enter()/exit(), same posture as m_tagBehaviour above.
    MediaViewerPreviewMode m_previewMode = MediaViewerPreviewMode::Static;
    // Folder-Grouped Browsing (Architecture.md §14.74). m_groupMode is a toolbar preference like
    // m_sortCriterion/m_viewMode above -- not reset by enter()/exit(). m_currentFolder/
    // m_folderHistory are a second, root-confined navigation state scoped to Folder-mode
    // sub-browsing, reset to m_root by enter() and whenever setGroupMode() switches into ByFolder
    // -- kept entirely separate from TabViewModel's own history/currentPath() so exiting Media
    // Viewer is unaffected.
    MediaViewerGroupMode m_groupMode = MediaViewerGroupMode::Recursive;
    std::filesystem::path m_currentFolder;
    NavigationHistory m_folderHistory;
    std::filesystem::path m_root;
    std::vector<Tag::Id> m_tagIds;
    QString m_nameQuery;
    NameSearchOptions m_nameOptions;
    // Cached candidate set: post-tag-filter (or the whole recursive scan when no tags are
    // applied), pre-kind/name-filter -- re-fetched only when m_tagIds changes; applyFilter()
    // re-filters this in memory for every kind/name-filter edit.
    std::vector<FileNode> m_candidates;
    // Bumped on every candidate-fetch dispatch and on exit(); discards a stale reply the same way
    // TabQuickSearchViewModel's and TabAdvancedSearchViewModel's own generation counters do for
    // their own replies.
    int m_generation = 0;
    // The copy-style progress notification driving entering Media Viewer mode (Architecture.md
    // §14.65) -- 0 means none currently active. Owned/driven directly by this class, same posture
    // FileOperationsController has for copy/move's progress id.
    NotificationCenter::ProgressId m_progressId = 0;
    std::size_t m_thumbnailsTotal = 0;
    std::size_t m_thumbnailsDone = 0;
    // De-duplicates m_thumbnailsDone's advance (Architecture.md §14.66): a path that the kind's
    // background sweep (Low priority) and the current filter match (High priority) both cover
    // would otherwise have its decode counted twice against the same total. Reset alongside
    // m_thumbnailsTotal/Done whenever a sweep establishes a new total.
    QSet<QString> m_countedPaths;
    // Per-generation "has this kind's whole-kind background sweep already been dispatched"
    // tracker (Architecture.md §14.66), reset in onCandidatesReady whenever m_candidates is
    // replaced by a fresh fetch. Guards requestKindSweep against a redundant re-dispatch, and lets
    // setKind tell whether switching to `kind` needs to kick off a real decode (never swept yet)
    // or is a cheap re-filter of already-decoded entries (already swept).
    bool m_imageSweepRequested = false;
    bool m_videoSweepRequested = false;
    bool m_animationSweepRequested = false;
};
