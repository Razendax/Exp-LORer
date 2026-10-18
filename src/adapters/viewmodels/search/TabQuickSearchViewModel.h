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
#include "NameSearchOptions.h"
#include "Result.h"

class AdvancedSearchService;
class FileDecorationRules;
class FileTagsService;
class IconLoadingService;
class TagChangeNotifier;

// Quick Search ("Search tab") state for a single tab (Architecture.md §14.42), extracted out of
// TabViewModel (Architecture.md §14.96). Owned by TabViewModel via `this`-parenting, exposed
// through TabViewModel::quickSearch(); callers call its slots and connect to its signals directly
// rather than through TabViewModel forwarders. updateQuery/refresh/setOptions take `currentPath`
// as a parameter since navigation state isn't part of this sub-object -- mirrors
// TabMonitoringViewModel::start(). The single-active-mode invariant with Advanced Search
// (exitAdvancedSearch()) stays TabViewModel's responsibility, via its own thin
// updateSearchQuery()/refreshSearch() wrappers, since Advanced Search isn't extracted yet.
class TabQuickSearchViewModel : public QObject
{
    Q_OBJECT

public:
    TabQuickSearchViewModel(AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                             IconLoadingService& iconLoadingService, TagChangeNotifier& tagChangeNotifier,
                             const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                             QObject* parent = nullptr);

    FileListModel* resultsModel() const noexcept { return m_resultsModel; }
    bool isActive() const noexcept { return m_active; }
    QString query() const noexcept { return m_query; }

    // Match Case/Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles (Architecture.md
    // §14.44/§14.45).
    NameSearchOptions options() const noexcept { return { m_matchMode, m_caseSensitive, m_skipGitIgnored }; }

    // Icons resolved so far for the current candidate list (Architecture.md §14.42) -- see
    // resultIconsChanged() for why callers need this instead of reading resultsModel() directly.
    const QHash<QString, QIcon>& resultIcons() const noexcept { return m_snapshotIcons; }

public slots:
    // Recursive filename search scoped to `currentPath` (Architecture.md §14.42), async via
    // AdvancedSearchService. Empty/whitespace-only query exits search (no-op if already inactive).
    // Otherwise: the first call against a given root dispatches one recursive scan; an edit that
    // arrives before that scan finishes re-filters its latest progress tick in memory instead of
    // starting a second concurrent scan of the same root; an edit after it finishes re-filters the
    // cached snapshot, no disk I/O. On a scan failure, emits failed and leaves prior state
    // untouched.
    void updateQuery(const QString& query, const std::filesystem::path& currentPath);

    // Forces a fresh recursive rescan of `currentPath` against `query` (Refresh button / Enter in
    // the search bar). Unlike updateQuery, this always (re)starts a search even if a prior search
    // was exited by an in-tab navigation. Empty/whitespace-only query exits search instead.
    void refresh(const QString& query, const std::filesystem::path& currentPath);

    // Updates the toggles and re-filters whatever candidate list (cached snapshot, or an in-flight
    // scan's latest partial snapshot) is already available in memory -- never dispatches a rescan
    // unless skipGitIgnored changed (it affects what a scan *visits*, not just how an
    // already-scanned list is filtered, so it forces one against `currentPath`). A no-op beyond
    // updating the toggles if the query is currently empty (nothing to filter yet).
    void setOptions(const NameSearchOptions& options, const std::filesystem::path& currentPath);

    // Clears search state and flips isActive() off. No-op if already inactive.
    void exit();

    // Re-requests icons at the new size tier for whatever's already loaded (TabViewModel's own
    // setViewMode, Architecture.md §14.71) -- caches `mask` unconditionally (even when inactive or
    // unchanged) so a later async icon-fetch completion always uses the live tier, even one from a
    // scan dispatched before the tier last changed.
    void setIconMask(IconSizeMask mask);

signals:
    void modeChanged(bool active);
    void resultsChanged(const std::vector<FileNode>& entries);
    // Fired whenever resultIcons() gains newly-resolved icons -- unlike resultsChanged, this does
    // NOT mean the candidate list itself changed, just that resultIcons() has more to offer.
    // MainWindow relays this to SearchPanelWidget so already-visible rows light up progressively
    // instead of only updating on the next query edit.
    void resultIconsChanged();
    void failed(const QString& message);
    // Emitted right before dispatching a recursive-scan request.
    void loading();
    // Emitted once the in-flight scan that most recently triggered loading() actually finishes
    // (successfully -- failed covers the error case); does NOT fire for a long scan's intermediate
    // progress ticks.
    void loadingFinished();

private:
    // Shared implementation of updateQuery/refresh: re-scans recursively only when forceRescan or
    // no valid cached/in-flight scan of the current root exists yet, otherwise re-filters whatever
    // candidate list is already available in memory -- never a second concurrent scan of the same
    // root.
    void run(const std::filesystem::path& currentPath, bool forceRescan);

    // Composes filterByName -> sortBy -> setEntries -> activate-if-needed -> emit resultsChanged,
    // the tail shared by the synchronous cached-snapshot path and both async completion callbacks
    // below.
    void applyFilter(const std::vector<FileNode>& candidates);

    // Completion callback for the recursive scan dispatched by run() (via
    // AdvancedSearchService::requestRecursiveScan). Discards the result if `generation` no longer
    // matches m_generation (a newer dispatch, or exit()/tab close, superseded it in the meantime).
    void onCandidatesReady(int generation, Result<std::vector<FileNode>> result);

    // Progress callback for AdvancedSearchService::requestRecursiveScan's onProgress
    // (Architecture.md §14.37): discarded the same way as onCandidatesReady via the generation
    // check. Caches `partial` as the best-known candidate list so far and re-filters it with
    // whatever m_query holds *right now* -- so a query edit that arrives while the scan is still
    // running is reflected on the very next tick.
    void onProgress(int generation, std::vector<FileNode> partial);

    // Same mechanism as TabViewModel::requestIcons, for this sub-object's own results model.
    // Discarded if `generation` no longer matches m_generation, same discard-by-generation-id
    // approach as onCandidatesReady/onProgress.
    void requestResultIcons(int generation, const std::vector<FileNode>& candidates);

    // Fetches tags for every file in `candidates` and, once they arrive (discarded if `generation`
    // no longer matches m_generation), caches them into m_snapshotTags and applies them to
    // m_resultsModel. Used whenever the candidate set itself changes -- applyFilter's own
    // re-filters reuse the cached map directly instead of re-dispatching.
    void requestResultTags(int generation, const std::vector<FileNode>& candidates);

    AdvancedSearchService& m_advancedSearchService;
    FileTagsService& m_fileTagsService;
    IconLoadingService& m_iconLoadingService;

    FileListModel* m_resultsModel = nullptr;
    bool m_active = false;
    std::filesystem::path m_root;
    std::vector<FileNode> m_snapshot;
    // True only right after a successful recursive scan has fully populated m_snapshot.
    bool m_fullScanDone = false;
    // True from the moment a recursive scan is dispatched until its completion callback runs (with
    // a matching generation) or exit() resets it. While true, a query edit (updateQuery,
    // forceRescan=false) does NOT dispatch a second concurrent scan of the same root; it just
    // re-filters m_partialSnapshot synchronously.
    bool m_scanPending = false;
    // Best-known candidate list from the in-flight scan's progress ticks (Architecture.md §14.37),
    // updated by onProgress.
    std::vector<FileNode> m_partialSnapshot;
    // Tags for whichever candidate list (m_snapshot or m_partialSnapshot) was most recently
    // fetched, fetched via requestResultTags whenever that candidate list itself changes and
    // reapplied as-is by every applyFilter re-filter -- a filtered subset never needs a fresh fetch
    // since it's always a subset of the same candidate list.
    TagsByPathMap m_snapshotTags;
    // Icons resolved so far for the current candidate list, accumulated chunk-by-chunk by
    // requestResultIcons and reapplied as-is by every applyFilter re-filter -- needed because
    // FileListModel::setEntries() (which every re-filter goes through) clears the model's own
    // per-path icon cache, so without this a query edit on an already-fully-scanned root would
    // flash every row back to its placeholder icon instead of keeping the ones IconLoadingService
    // already resolved.
    QHash<QString, QIcon> m_snapshotIcons;
    QString m_query;
    // Match Case/Whole Word/Exact Name/Match Regex toggle state, applied by applyFilter via
    // FileNavigationUseCase::filterByName's options-aware overload. Reset to their defaults by
    // exit(), mirroring m_query.
    NameMatchMode m_matchMode = NameMatchMode::Contains;
    bool m_caseSensitive = false;
    // Skip Git-Ignored toggle state (Architecture.md §14.45) -- unlike the two above, changing this
    // forces a rescan (setOptions), since it changes what a scan visits, not just how a candidate
    // list already in memory is filtered. Reset to false by exit().
    bool m_skipGitIgnored = false;
    // Bumped on every recursive-scan dispatch and on exit(); onCandidatesReady/onProgress discard
    // any reply whose generation no longer matches.
    int m_generation = 0;
    // Current icon size tier, pushed in by TabViewModel::setViewMode via setIconMask() -- defaults
    // to the tier matching TabViewModel's own ViewMode::Details default, so the two stay in sync
    // even before setViewMode is ever called.
    IconSizeMask m_iconMask = IconSizeMask::Small;
};
