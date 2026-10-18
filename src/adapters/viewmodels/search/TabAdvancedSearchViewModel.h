#pragma once

#include <filesystem>
#include <vector>

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

#include "FileListModel.h"
#include "FileTagsByPath.h"
#include "IconSizeMask.h"
#include "Result.h"
#include "SearchCriteria.h"
#include "Tag.h"

class AdvancedSearchService;
class FileDecorationRules;
class FileNavigationUseCase;
class FileTagsService;
class IconLoadingService;
class TagChangeNotifier;
class TagManagementUseCase;

// Advanced (criteria) search pane state for a single tab (Architecture.md §14.19), extracted out
// of TabViewModel (Architecture.md §14.96). Owned by TabViewModel via `this`-parenting, exposed
// through TabViewModel::advancedSearch(); callers call its slots and connect to its signals
// directly rather than through TabViewModel forwarders. showPanel/start/updateCriteria/
// addTagCriterion/removeTagCriterion take `currentPath` as a parameter since navigation state
// isn't part of this sub-object -- mirrors TabMonitoringViewModel::start(). The single-active-mode
// invariant with Quick Search (quickSearch()->exit()) stays TabViewModel's responsibility, via its
// own thin wrapper slots, same posture as TabQuickSearchViewModel's exitAdvancedSearch() calls.
class TabAdvancedSearchViewModel : public QObject
{
    Q_OBJECT

public:
    TabAdvancedSearchViewModel(FileNavigationUseCase& fileNavigationUseCase, AdvancedSearchService& advancedSearchService,
                                TagManagementUseCase& tagManagementUseCase, FileTagsService& fileTagsService,
                                IconLoadingService& iconLoadingService, TagChangeNotifier& tagChangeNotifier,
                                const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                                QObject* parent = nullptr);

    FileListModel* resultsModel() const noexcept { return m_resultsModel; }
    bool isActive() const noexcept { return m_active; }
    const SearchCriteria& criteria() const noexcept { return m_criteria; }

    // Resolves criteria().tagIds into full Tag objects (for chip display); filters
    // TagManagementUseCase::allTags(), empty vector on repository failure.
    std::vector<Tag> resolveTagCriteria() const;

public slots:
    // Reveals the pane with a plain non-recursive listing of `currentPath` (not a real search --
    // no snapshot is cached, no criteria applied) so it doesn't open empty. No-op if already
    // active.
    void showPanel(const std::filesystem::path& currentPath);

    // No-op if criteria is entirely empty (nameQuery/size bounds/extensions all unset). Otherwise
    // runs one recursive Port call against `currentPath`, caches the snapshot, filters + sorts it
    // into resultsModel(), and flips isActive() on.
    void start(const SearchCriteria& criteria, const std::filesystem::path& currentPath);

    // Re-filters the cached snapshot from the last start() call -- no disk I/O, unless
    // skipGitIgnored/tagIncludeChildTags changed (both affect what a scan/DB query actually
    // visits, not just how an already-fetched candidate list is filtered, so either forces a
    // rescan against `currentPath`). No-op if isActive() is false.
    void updateCriteria(const SearchCriteria& criteria, const std::filesystem::path& currentPath);

    // Clears advanced-search state and flips isActive() off. No-op if already inactive.
    void exit();

    // Adds tagId to the criteria (no-op if already present), opens the panel against
    // `currentPath` if it isn't already (single-active-mode invariant), and (re)runs the search.
    // Used by both the tag panel's chip-click path and Advanced Search's own tag-search box.
    void addTagCriterion(Tag::Id tagId, const std::filesystem::path& currentPath);

    // Removes tagId from the criteria and re-runs the search against `currentPath` if the panel is
    // active; no-op otherwise or if tagId isn't present.
    void removeTagCriterion(Tag::Id tagId, const std::filesystem::path& currentPath);

    // Re-requests icons at the new size tier for whatever's already loaded (TabViewModel's own
    // setViewMode, Architecture.md §14.71) -- caches `mask` unconditionally so a later async
    // icon-fetch completion always uses the live tier. Mirrors TabQuickSearchViewModel::
    // setIconMask.
    void setIconMask(IconSizeMask mask);

signals:
    void modeChanged(bool active);
    void resultsChanged(const std::vector<FileNode>& entries);
    void failed(const QString& message);

    // Emitted right before dispatching a recursive-scan or tag-first-candidate request, so the UI
    // can show a busy state (Architecture.md §14.37).
    void loading();

    // Emitted once the in-flight scan/candidate fetch that most recently triggered loading()
    // actually finishes (successfully -- failed covers the error case). Unlike resultsChanged,
    // this does NOT fire for the intermediate progress ticks a long recursive scan reports along
    // the way, so a busy indicator wired to loading/this pair stays up for the whole scan rather
    // than clearing after the first partial result.
    void loadingFinished();

    // Lets the UI re-sync its tag-criteria chip row (and, harmlessly, its Name/size/extension
    // fields, already idempotent via QSignalBlocker in SearchCriteriaPanel::setCriteria)
    // regardless of *which* entry point changed the criteria (its own fields, its own tag-search
    // box, or a right-panel tag click on a completely different widget).
    void criteriaChanged(const SearchCriteria& criteria);

private:
    // Shared by showPanel/start/updateCriteria/removeTagCriterion for the "criteria is (now)
    // empty" case (Architecture.md §14.56): shows a plain non-recursive listing of `currentPath`
    // and resets every cached-scan member (mirrors exit()'s cleanup) while keeping isActive() true,
    // so the pane stays open instead of closing. Emits resultsChanged/criteriaChanged at the end so
    // the status bar count and the panel's tag-chip row stay in sync even when reached from a live
    // edit rather than showPanel() itself. On a listDirectory failure, emits failed and leaves
    // prior state untouched.
    void showCurrentFolderNonRecursive(const std::filesystem::path& currentPath);

    // Shared implementation of start/updateCriteria/addTagCriterion/removeTagCriterion. When
    // criteria.tagIds is non-empty, the search is tag-first: queries
    // TagManagementUseCase::findFilesWithAllTags directly (no recursive scan), scopes the result to
    // `currentPath` via isPathWithinOrEqual, stat()s each surviving path, and applies
    // filterByCriteria as a secondary name/size filter. Otherwise, re-scans recursively only when
    // forceRescan or no valid cached scan exists yet (m_fullScanDone), then applies
    // filterByCriteria. Both branches converge on sortBy -> resultsModel()->setEntries and emit the
    // result + criteria-changed signals (Architecture.md §14.22).
    void run(bool forceRescan, const std::filesystem::path& currentPath);

    // Composes filterByCriteria -> sortBy -> setEntries -> activate-if-needed -> emit
    // resultsChanged/criteriaChanged, the tail shared by the synchronous cached-snapshot path and
    // both async completion callbacks below.
    void applyFilter(const std::vector<FileNode>& candidates);

    // Completion callback for AdvancedSearchService::requestRecursiveScan/
    // requestTagFirstCandidates. Discards the result if `generation` no longer matches
    // m_generation (a newer request superseded it, or the search/tab was closed in the meantime).
    // isNewSnapshot distinguishes a fresh recursive-scan result (cached into m_snapshot) from a
    // tag-first result (applied directly, never cached).
    void onCandidatesReady(int generation, Result<std::vector<FileNode>> result, bool isNewSnapshot);

    // Progress callback for AdvancedSearchService::requestRecursiveScan's onProgress
    // (Architecture.md §14.37): discarded the same way as onCandidatesReady via the generation
    // check. Caches `partial` as the best-known candidate list so far and re-filters it with
    // whatever m_criteria holds *right now* -- so a criteria edit that arrives while the scan is
    // still running is reflected on the very next tick without needing its own rescan.
    void onProgress(int generation, std::vector<FileNode> partial);

    // Same mechanism as TabViewModel::requestIcons, for this sub-object's own results model.
    void requestResultIcons(int generation, const std::vector<FileNode>& candidates);

    // Same shape as TabQuickSearchViewModel::requestResultTags, for this sub-object's own results
    // model.
    void requestResultTags(int generation, const std::vector<FileNode>& candidates);

    FileNavigationUseCase& m_fileNavigationUseCase;
    AdvancedSearchService& m_advancedSearchService;
    TagManagementUseCase& m_tagManagementUseCase;
    FileTagsService& m_fileTagsService;
    IconLoadingService& m_iconLoadingService;

    FileListModel* m_resultsModel = nullptr;
    bool m_active = false;
    SearchCriteria m_criteria;
    std::filesystem::path m_root;
    std::vector<FileNode> m_snapshot;
    // True only right after a successful listDirectoryRecursive has populated m_snapshot.
    // m_root.empty() alone can no longer signal "have we ever scanned", since the tag-first branch
    // also sets m_root (for scoping) without ever performing a recursive scan.
    bool m_fullScanDone = false;
    // True from the moment a recursive scan is dispatched until its completion callback runs (with
    // a matching generation) or exit() resets it. While true, a criteria edit (updateCriteria/
    // removeTagCriterion, forceRescan=false) does NOT dispatch a second concurrent scan of the same
    // root -- it just re-filters m_partialSnapshot synchronously, since the one scan already
    // running will itself keep reporting progress (and eventually the final result) filtered
    // against whatever criteria is current when each tick fires (Architecture.md §14.37).
    bool m_scanPending = false;
    // Best-known candidate list from the in-flight scan's progress ticks (Architecture.md §14.37),
    // updated by onProgress. Distinct from m_snapshot, which is only ever written once a scan has
    // *fully* completed.
    std::vector<FileNode> m_partialSnapshot;
    // Tags for whichever candidate list (m_snapshot or m_partialSnapshot) was most recently
    // fetched, fetched via requestResultTags whenever that candidate list itself changes and
    // reapplied as-is by every applyFilter re-filter.
    TagsByPathMap m_snapshotTags;
    // Icons resolved so far for the current candidate list, accumulated chunk-by-chunk by
    // requestResultIcons and reapplied as-is by every applyFilter re-filter -- mirrors
    // m_snapshotTags; see TabQuickSearchViewModel's m_snapshotIcons for why this is needed.
    QHash<QString, QIcon> m_snapshotIcons;
    // The tagIds that m_snapshot/m_fullScanDone/m_scanPending currently pertain to when they were
    // populated by the tag-first branch of run() (empty when they instead pertain to a plain
    // recursive scan of m_root). Lets a Name/size-only criteria edit with the same tag selection
    // reuse the cached/in-flight tag-first candidates instead of re-dispatching a redundant DB
    // query + per-file stat loop.
    std::vector<Tag::Id> m_cachedTagIds;
    // Whether m_cachedTagIds' candidates were fetched with the Exact/Include-Children toggle
    // (Architecture.md §14.87) set to "include children". Part of the same cache key as
    // m_cachedTagIds -- toggling it must invalidate the cache even when the tag selection itself
    // hasn't changed, since it changes which DB answer applies to that selection.
    bool m_cachedTagIncludeChildren = false;
    // Bumped every time a recursive-scan or tag-first-candidate request is dispatched, and on
    // exit(); onCandidatesReady/onProgress discard any reply whose generation no longer matches,
    // so a stale result from a superseded/abandoned request can't resurrect it.
    int m_generation = 0;
    // Current icon size tier, pushed in by TabViewModel::setViewMode via setIconMask() -- defaults
    // to the tier matching TabViewModel's own ViewMode::Details default, mirrors
    // TabQuickSearchViewModel::m_iconMask.
    IconSizeMask m_iconMask = IconSizeMask::Small;
};
