#include "TabViewModel.h"

#include <algorithm>
#include <utility>

#include <QPointer>

#include "AdvancedSearchService.h"
#include "ArchiveExtensions.h"
#include "DiskUsageNode.h"
#include "DiskUsageService.h"
#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "FileTagsByPath.h"
#include "FileTagsService.h"
#include "FolderSizesByPath.h"
#include "GitDirectoryStatus.h"
#include "GitStatusService.h"
#include "TagChangeNotifier.h"
#include "TagManagementUseCase.h"
#include "VirtualPaths.h"

namespace
{
    bool isCriteriaEmpty(const SearchCriteria& criteria)
    {
        return criteria.nameQuery.empty() && !criteria.minSizeBytes && !criteria.maxSizeBytes && criteria.tagIds.empty();
    }

    constexpr std::size_t kMonitoringLogCap = 200;
}

TabViewModel::TabViewModel(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                           const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                           GitStatusService& gitStatusService, FolderMonitorUseCase& folderMonitorUseCase,
                           AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                           DiskUsageService& diskUsageService, TagChangeNotifier& tagChangeNotifier, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_gitStatusService(gitStatusService)
    , m_folderMonitorUseCase(folderMonitorUseCase)
    , m_advancedSearchService(advancedSearchService)
    , m_fileTagsService(fileTagsService)
    , m_diskUsageService(diskUsageService)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_fileListModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_searchResultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_advancedSearchResultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
{
    connect(this, &TabViewModel::directoryContentsChanged, m_fileListModel, &FileListModel::setEntries);
    connect(m_fileListModel, &FileListModel::sortOrderChanged, this, &TabViewModel::sortOrderChanged);

    // Normal browsing only (Architecture.md §14.x's v1 scope cut) -- quick-search/advanced-search
    // results fire their own searchResultsChanged/advancedSearchResultsChanged signals instead,
    // which this deliberately does not connect to.
    connect(this, &TabViewModel::directoryContentsChanged, this,
            [this](const std::filesystem::path& path, const std::vector<FileNode>& entries) {
                requestGitStatus(path);
                requestGitRepositorySummary(path);
                requestFileTags(path, entries);
                requestFolderSizes(path, entries);
            });

    // Architecture.md §14.x: a tag assigned/removed/renamed/deleted anywhere re-fetches this tab's
    // currently-listed directory's tags, not just the path that actually changed -- correctness
    // over precision, since the alternative is threading which-path-changed information through a
    // signal meant to stay a plain broadcast. Also refreshes whichever of search/advanced search is
    // currently active, for the same reason.
    connect(&m_tagChangeNotifier, &TagChangeNotifier::tagsChanged, this, [this]() {
        requestFileTags(currentPath(), m_fileListModel->entries());

        if (m_searchActive)
        {
            requestSearchResultTags(m_searchGeneration, m_searchSnapshot);
        }

        if (m_advancedSearchActive)
        {
            const std::vector<FileNode>& candidates =
                m_advancedSearchFullScanDone ? m_advancedSearchSnapshot : m_advancedSearchPartialSnapshot;
            requestAdvancedSearchResultTags(m_advancedSearchGeneration, candidates);
        }
    });
}

TabViewModel::~TabViewModel()
{
    // A still-active monitor is normally stopped by WorkspacePaneViewModel::closeTab() before a tab
    // is destroyed, but guard here too for any other destruction path. A monitor whose
    // startMonitoring() open is still in flight (StandardFolderMonitor now opens the directory
    // handle on its own worker thread, not inline) needs no equivalent handling here: its
    // completion callback captures a QPointer guard on `this`, not `this` itself, and stops the
    // now-ownerless monitor by that captured pointer directly if the guard is already null by the
    // time the open finishes -- see startMonitoring()'s comment.
    if (m_monitoringActive)
    {
        m_folderMonitorUseCase.stopMonitoring(m_monitoringId);
    }
}

std::filesystem::path TabViewModel::currentPath() const
{
    return m_history.current().value_or(std::filesystem::path());
}

bool TabViewModel::backAvailable() const
{
    return m_history.canGoBack();
}

bool TabViewModel::forwardAvailable() const
{
    return m_history.canGoForward();
}

bool TabViewModel::upAvailable() const
{
    const auto current = m_history.current();
    return current.has_value() && *current != VirtualPaths::ThisPC;
}

SortCriterion TabViewModel::sortCriterion() const noexcept
{
    return m_fileListModel->sortCriterion();
}

bool TabViewModel::sortAscending() const noexcept
{
    return m_fileListModel->sortAscending();
}

QStringList TabViewModel::suggestFolders(const std::filesystem::path& directory) const
{
    auto result = m_fileNavigationUseCase.listDirectory(directory);
    if (result.hasError())
    {
        return {};
    }

    const bool showHidden = FileListModel::showHiddenFilesEnabled();

    QStringList names;
    for (const FileNode& entry : result.value())
    {
        if (entry.isDirectory() && (showHidden || !entry.isHidden()))
        {
            names.append(QString::fromStdWString(entry.name().wstring()));
        }
    }

    names.sort(Qt::CaseInsensitive);
    return names;
}

void TabViewModel::navigateTo(const std::filesystem::path& path)
{
    exitSearch();
    exitAdvancedSearch();
    loadAndApply(path, HistoryUpdate::Record);
}

std::filesystem::path TabViewModel::upTarget() const
{
    const auto current = m_history.current();
    if (!current)
    {
        return std::filesystem::path();
    }

    if (*current == VirtualPaths::ThisPC)
    {
        // No legal parent -- returning the current path itself signals "nothing to navigate to".
        return *current;
    }

    const auto parent = current->parent_path();
    if (parent == *current)
    {
        // A filesystem root (local drive root or UNC share root) — This PC is its "exit" point.
        return VirtualPaths::ThisPC;
    }

    return parent;
}

std::optional<std::filesystem::path> TabViewModel::peekBack() const
{
    return m_history.peekBack();
}

std::optional<std::filesystem::path> TabViewModel::peekForward() const
{
    return m_history.peekForward();
}

void TabViewModel::goUp()
{
    const std::filesystem::path target = upTarget();
    if (target == currentPath())
    {
        return;
    }

    navigateTo(target);
}

void TabViewModel::goBack()
{
    exitSearch();
    exitAdvancedSearch();

    const auto path = m_history.goBack();
    if (!path)
    {
        return;
    }

    loadAndApply(*path, HistoryUpdate::None);
}

void TabViewModel::goForward()
{
    exitSearch();
    exitAdvancedSearch();

    const auto path = m_history.goForward();
    if (!path)
    {
        return;
    }

    loadAndApply(*path, HistoryUpdate::None);
}

void TabViewModel::setSelectedEntries(const std::vector<FileNode>& entries)
{
    if (m_selectedEntries == entries)
    {
        return;
    }

    m_selectedEntries = entries;
    emit selectedEntriesChanged(entries);
}

void TabViewModel::refresh()
{
    loadAndApply(currentPath(), HistoryUpdate::None);
}

void TabViewModel::replaceCurrentPath(const std::filesystem::path& path)
{
    exitSearch();
    exitAdvancedSearch();
    loadAndApply(path, HistoryUpdate::ReplaceCurrent);
}

void TabViewModel::setViewMode(ViewMode mode)
{
    if (mode == m_viewMode)
    {
        return;
    }

    m_viewMode = mode;
    emit viewModeChanged(mode);
}

void TabViewModel::setSortCriterion(SortCriterion criterion, bool ascending)
{
    m_fileListModel->setSortCriterion(criterion, ascending);
}

void TabViewModel::loadAndApply(const std::filesystem::path& path, HistoryUpdate historyUpdate)
{
    auto result = m_fileNavigationUseCase.listDirectory(path);
    if (result.hasError())
    {
        emit navigationFailed(path, QString::fromStdString(result.error().message));
        return;
    }

    if (historyUpdate == HistoryUpdate::Record)
    {
        m_history.navigate(path);
    }
    else if (historyUpdate == HistoryUpdate::ReplaceCurrent)
    {
        m_history.replaceCurrent(path);
    }

    setSelectedEntries({});

    emit currentPathChanged(path);
    emit directoryContentsChanged(path, result.value());
    emitAvailability();
}

void TabViewModel::startSearch(const QString& query)
{
    if (query.trimmed().isEmpty())
    {
        return;
    }

    exitAdvancedSearch();

    m_searchRoot = currentPath();
    m_searchQuery = query;

    const int generation = ++m_searchGeneration;

    // Recursive scan runs off the Qt UI thread via AdvancedSearchService's worker -- same scan
    // FileNavigationUseCase::listDirectoryRecursive would have done synchronously here before.
    m_advancedSearchService.requestRecursiveScan(this, m_searchRoot, [this, generation](Result<std::vector<FileNode>> result) {
        onSearchScanReady(generation, std::move(result));
    });
}

void TabViewModel::onSearchScanReady(int generation, Result<std::vector<FileNode>> result)
{
    if (generation != m_searchGeneration)
    {
        // Superseded by a newer startSearch() call, or the search was exited/tab closed in the
        // meantime -- discard.
        return;
    }

    if (result.hasError())
    {
        emit searchFailed(QString::fromStdString(result.error().message));
        return;
    }

    m_searchSnapshot = std::move(result).value();
    m_searchActive = true;

    auto filtered = FileNavigationUseCase::filterByName(m_searchSnapshot, m_searchQuery.toStdString());
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_searchResultsModel->setEntries(m_searchRoot, filtered);
    requestSearchResultTags(generation, m_searchSnapshot);

    setSelectedEntries({});
    emit searchModeChanged(true);
    emit searchResultsChanged(filtered);
}

void TabViewModel::updateSearchQuery(const QString& query)
{
    if (!m_searchActive)
    {
        return;
    }

    m_searchQuery = query;

    auto filtered = FileNavigationUseCase::filterByName(m_searchSnapshot, query.toStdString());
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_searchResultsModel->setEntries(m_searchRoot, filtered);
    // setEntries() above clears the model's tags -- m_searchSnapshotTags already has every tag for
    // the whole snapshot (requestSearchResultTags fetched it once when the snapshot itself was set),
    // so re-applying it here needs no new DB round trip even though this runs on every keystroke.
    m_searchResultsModel->setFileTags(m_searchSnapshotTags);

    emit searchResultsChanged(filtered);
}

void TabViewModel::exitSearch()
{
    if (!m_searchActive)
    {
        return;
    }

    m_searchActive = false;
    m_searchSnapshot.clear();
    m_searchSnapshotTags.clear();
    m_searchQuery.clear();
    m_searchRoot.clear();
    ++m_searchGeneration;

    setSelectedEntries({});
    emit searchModeChanged(false);
}

void TabViewModel::showAdvancedSearchPanel()
{
    if (m_advancedSearchActive)
    {
        return;
    }

    exitSearch();

    // Not a real (recursive) search -- just a non-recursive listing of the current folder so the
    // pane doesn't open empty. m_advancedSearchRoot/m_advancedSearchSnapshot stay unset, so the
    // first real search (startAdvancedSearch/updateAdvancedSearchCriteria/addTagSearchCriterion)
    // still performs its own recursive scan.
    const std::filesystem::path path = currentPath();
    auto result = m_fileNavigationUseCase.listDirectory(path);
    if (result.hasError())
    {
        emit advancedSearchFailed(QString::fromStdString(result.error().message));
        return;
    }

    auto sorted = FileNavigationUseCase::sortBy(std::move(result).value(), SortCriterion::Name, true);
    m_advancedSearchResultsModel->setEntries(path, sorted);
    requestAdvancedSearchResultTags(m_advancedSearchGeneration, sorted);

    m_advancedSearchActive = true;
    setSelectedEntries({});
    emit advancedSearchModeChanged(true);
}

void TabViewModel::startAdvancedSearch(const SearchCriteria& criteria)
{
    if (isCriteriaEmpty(criteria))
    {
        return;
    }

    exitSearch();

    m_advancedSearchCriteria = criteria;
    runAdvancedSearch(/*forceRescan=*/true);
}

void TabViewModel::updateAdvancedSearchCriteria(const SearchCriteria& criteria)
{
    if (!m_advancedSearchActive)
    {
        return;
    }

    m_advancedSearchCriteria = criteria;
    runAdvancedSearch(/*forceRescan=*/false);
}

void TabViewModel::exitAdvancedSearch()
{
    if (!m_advancedSearchActive)
    {
        return;
    }

    m_advancedSearchActive = false;
    m_advancedSearchSnapshot.clear();
    m_advancedSearchPartialSnapshot.clear();
    m_advancedSearchSnapshotTags.clear();
    m_advancedSearchCriteria = SearchCriteria();
    m_advancedSearchRoot.clear();
    m_advancedSearchCachedTagIds.clear();
    m_advancedSearchFullScanDone = false;
    m_advancedSearchScanPending = false;
    // Invalidates any scan/stat-loop still in flight -- its (now-stale) result is discarded by
    // onAdvancedSearchCandidatesReady/onAdvancedSearchProgress's generation check when it
    // eventually arrives.
    ++m_advancedSearchGeneration;

    setSelectedEntries({});
    emit advancedSearchModeChanged(false);
}

void TabViewModel::addTagSearchCriterion(Tag::Id tagId)
{
    if (std::find(m_advancedSearchCriteria.tagIds.begin(), m_advancedSearchCriteria.tagIds.end(), tagId) !=
        m_advancedSearchCriteria.tagIds.end())
    {
        return;
    }

    const bool wasActive = m_advancedSearchActive;
    if (!wasActive)
    {
        exitSearch();
        m_advancedSearchActive = true;
        setSelectedEntries({});
        emit advancedSearchModeChanged(true);
    }

    m_advancedSearchCriteria.tagIds.push_back(tagId);
    runAdvancedSearch(/*forceRescan=*/!wasActive);
}

void TabViewModel::removeTagSearchCriterion(Tag::Id tagId)
{
    auto& tagIds = m_advancedSearchCriteria.tagIds;
    const auto it = std::find(tagIds.begin(), tagIds.end(), tagId);
    if (it == tagIds.end())
    {
        return;
    }

    tagIds.erase(it);

    if (m_advancedSearchActive)
    {
        runAdvancedSearch(/*forceRescan=*/false);
    }
}

std::vector<Tag> TabViewModel::searchTagsForCriteria(const QString& query) const
{
    auto result = m_tagManagementUseCase.searchTags(query.toStdString());
    if (result.hasError())
    {
        return {};
    }
    return result.value();
}

std::vector<Tag> TabViewModel::resolveTagCriteria() const
{
    auto result = m_tagManagementUseCase.allTags();
    if (result.hasError())
    {
        return {};
    }

    std::vector<Tag> resolved;
    for (const Tag& tag : result.value())
    {
        if (std::find(m_advancedSearchCriteria.tagIds.begin(), m_advancedSearchCriteria.tagIds.end(), tag.id()) !=
            m_advancedSearchCriteria.tagIds.end())
        {
            resolved.push_back(tag);
        }
    }
    return resolved;
}

Result<DriveSpaceInfo> TabViewModel::driveSpaceInfo() const
{
    return m_fileNavigationUseCase.driveSpaceInfo(currentPath());
}

void TabViewModel::runAdvancedSearch(bool forceRescan)
{
    if (!m_advancedSearchCriteria.tagIds.empty())
    {
        // Tag-first path (Architecture.md §14.22): the tag DB alone answers "which files have
        // this tag" -- no recursive folder scan needed. m_advancedSearchRoot is still set (for
        // scoping + setEntries's root arg).
        const std::vector<Tag::Id>& tagIds = m_advancedSearchCriteria.tagIds;

        if (!forceRescan && m_advancedSearchCachedTagIds == tagIds && (m_advancedSearchFullScanDone || m_advancedSearchScanPending))
        {
            // Same tag set already fetched, or a fetch for it is already in flight (the user kept
            // typing/adjusting the name/size fields while it runs) -- don't re-dispatch a redundant
            // DB query + per-file stat loop. Just re-filter whatever candidates are available now;
            // an in-flight fetch's own completion callback will apply the latest criteria once it
            // arrives, same "reuse the in-flight request" posture as the non-tag branch below.
            applyAdvancedSearchFilter(m_advancedSearchFullScanDone ? m_advancedSearchSnapshot : m_advancedSearchPartialSnapshot);
            return;
        }

        m_advancedSearchRoot = currentPath();
        m_advancedSearchCachedTagIds = tagIds;
        m_advancedSearchFullScanDone = false;
        m_advancedSearchScanPending = true;
        m_advancedSearchPartialSnapshot.clear();

        const int generation = ++m_advancedSearchGeneration;

        emit advancedSearchLoading();
        m_advancedSearchService.requestTagFirstCandidates(
            this, m_advancedSearchRoot, tagIds, [this, generation](Result<std::vector<FileNode>> result) {
                onAdvancedSearchCandidatesReady(generation, std::move(result), /*isNewSnapshot=*/true);
            });
        return;
    }

    // m_advancedSearchCachedTagIds non-empty here means whatever m_advancedSearchSnapshot/
    // m_advancedSearchFullScanDone/m_advancedSearchScanPending currently hold pertains to the
    // tag-first branch above (a tag-restricted candidate set), not a real recursive scan of
    // m_advancedSearchRoot -- reusing it here (now that tagIds is empty) would silently omit every
    // untagged file. Treat it the same as "no scan cached yet" so a fresh full scan is dispatched.
    const bool cachedResultIsTagFirst = !m_advancedSearchCachedTagIds.empty();

    if (forceRescan || m_advancedSearchRoot.empty() || cachedResultIsTagFirst ||
        (!m_advancedSearchFullScanDone && !m_advancedSearchScanPending))
    {
        // No scan covering the current root is running or cached -- dispatch a fresh one. This also
        // covers forceRescan explicitly re-scanning a root that *was* already fully/partially
        // scanned (e.g. Search clicked again, or the criteria switched away from tags and back).
        m_advancedSearchRoot = currentPath();
        // m_advancedSearchSnapshot/m_advancedSearchFullScanDone/m_advancedSearchScanPending are
        // about to be overwritten with this recursive scan's own results -- invalidate the tag-first
        // cache-key above so it can't later be mistaken for a still-valid tag-first candidate cache.
        m_advancedSearchCachedTagIds.clear();
        m_advancedSearchFullScanDone = false;
        m_advancedSearchPartialSnapshot.clear();
        m_advancedSearchScanPending = true;

        const int generation = ++m_advancedSearchGeneration;

        emit advancedSearchLoading();
        m_advancedSearchService.requestRecursiveScan(
            this, m_advancedSearchRoot,
            [this, generation](Result<std::vector<FileNode>> result) {
                onAdvancedSearchCandidatesReady(generation, std::move(result), /*isNewSnapshot=*/true);
            },
            [this, generation](std::vector<FileNode> partial) { onAdvancedSearchProgress(generation, std::move(partial)); });
        return;
    }

    if (!m_advancedSearchFullScanDone && m_advancedSearchScanPending)
    {
        // A recursive scan of this same root is already running (the user kept typing/adjusting
        // criteria while it's in flight) -- don't start a second, redundant walk of the same
        // subtree. Just re-filter whatever the in-flight scan has reported so far; its own next
        // progress tick (or final result) will pick up m_advancedSearchCriteria as it stands then,
        // so the latest criteria is never lost (Architecture.md §14.37).
        applyAdvancedSearchFilter(m_advancedSearchPartialSnapshot);
        return;
    }

    // Cached, no rescan needed -- no disk I/O, so this stays fully synchronous (the common
    // "user is typing in the Name field" case).
    applyAdvancedSearchFilter(m_advancedSearchSnapshot);
}

void TabViewModel::applyAdvancedSearchFilter(const std::vector<FileNode>& candidates)
{
    auto filtered = FileNavigationUseCase::filterByCriteria(candidates, m_advancedSearchCriteria);
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_advancedSearchResultsModel->setEntries(m_advancedSearchRoot, filtered);
    // setEntries() above clears the model's tags -- m_advancedSearchSnapshotTags already has every
    // tag for the current candidate list (requestAdvancedSearchResultTags fetched it whenever that
    // list itself last changed), so re-applying it here needs no new DB round trip even though this
    // runs on every criteria edit.
    m_advancedSearchResultsModel->setFileTags(m_advancedSearchSnapshotTags);

    if (!m_advancedSearchActive)
    {
        m_advancedSearchActive = true;
        setSelectedEntries({});
        emit advancedSearchModeChanged(true);
    }

    emit advancedSearchResultsChanged(filtered);
    emit advancedSearchCriteriaChanged(m_advancedSearchCriteria);
}

void TabViewModel::onAdvancedSearchCandidatesReady(int generation, Result<std::vector<FileNode>> result, bool isNewSnapshot)
{
    if (generation != m_advancedSearchGeneration)
    {
        // Superseded by a newer request, or the search/tab was closed in the meantime -- discard.
        return;
    }

    if (isNewSnapshot)
    {
        m_advancedSearchScanPending = false;
    }

    if (result.hasError())
    {
        emit advancedSearchFailed(QString::fromStdString(result.error().message));
        return;
    }

    if (isNewSnapshot)
    {
        m_advancedSearchSnapshot = std::move(result).value();
        m_advancedSearchPartialSnapshot.clear();
        m_advancedSearchFullScanDone = true;
        applyAdvancedSearchFilter(m_advancedSearchSnapshot);
        requestAdvancedSearchResultTags(generation, m_advancedSearchSnapshot);
    }
    else
    {
        applyAdvancedSearchFilter(result.value());
    }

    emit advancedSearchLoadingFinished();
}

void TabViewModel::onAdvancedSearchProgress(int generation, std::vector<FileNode> partial)
{
    if (generation != m_advancedSearchGeneration)
    {
        // Superseded/abandoned, same as onAdvancedSearchCandidatesReady -- discard.
        return;
    }

    m_advancedSearchPartialSnapshot = std::move(partial);
    applyAdvancedSearchFilter(m_advancedSearchPartialSnapshot);
    requestAdvancedSearchResultTags(generation, m_advancedSearchPartialSnapshot);
}

void TabViewModel::emitAvailability()
{
    emit backAvailableChanged(backAvailable());
    emit forwardAvailableChanged(forwardAvailable());
    emit upAvailableChanged(upAvailable());
}

void TabViewModel::requestGitStatus(const std::filesystem::path& path)
{
    m_gitStatusService.requestStatus(this, path, [this, path](Result<GitDirectoryStatus> result) {
        if (path != currentPath())
        {
            // A newer navigation has since happened; this reply is stale.
            return;
        }

        m_fileListModel->setGitStatuses(result ? result.value().statusByPath : GitStatusByPathMap{});
    });
}

void TabViewModel::requestGitRepositorySummary(const std::filesystem::path& path)
{
    m_gitStatusService.requestRepositorySummary(this, path, [this, path](Result<GitRepositorySummary> result) {
        if (path != currentPath())
        {
            // A newer navigation has since happened; this reply is stale.
            return;
        }

        m_lastGitRepositorySummary = result ? result.value() : GitRepositorySummary{};
        emit gitRepositorySummaryChanged(m_lastGitRepositorySummary);
    });
}

void TabViewModel::requestFileTags(const std::filesystem::path& path, const std::vector<FileNode>& entries)
{
    m_fileTagsService.requestTags(this, entries, [this, path](Result<TagsByPathMap> result) {
        if (path != currentPath())
        {
            // A newer navigation has since happened; this reply is stale (only possible for the
            // tagsChanged() handler -- the directoryContentsChanged funnel always calls with the
            // just-navigated-to path already current).
            return;
        }

        m_fileListModel->setFileTags(result ? result.value() : TagsByPathMap{});
    });
}

void TabViewModel::requestFolderSizes(const std::filesystem::path& path, const std::vector<FileNode>& entries)
{
    const bool hasSubdirectories =
        std::any_of(entries.begin(), entries.end(), [](const FileNode& entry) { return entry.isDirectory(); });
    if (!hasSubdirectories)
    {
        m_fileListModel->setFolderSizes({});
        return;
    }

    m_diskUsageService.requestChildren(this, path, [this, path](Result<std::vector<DiskUsageNode>> result) {
        if (path != currentPath())
        {
            // A newer navigation has since happened; this reply is stale.
            return;
        }

        FolderSizesByPathMap sizes;
        if (result)
        {
            for (const DiskUsageNode& node : result.value())
            {
                if (node.isDirectory)
                {
                    sizes.emplace(node.path, node.size);
                }
            }
        }
        m_fileListModel->setFolderSizes(sizes);
    });
}

void TabViewModel::requestSearchResultTags(int generation, const std::vector<FileNode>& candidates)
{
    m_fileTagsService.requestTags(this, candidates, [this, generation](Result<TagsByPathMap> result) {
        if (generation != m_searchGeneration)
        {
            // Superseded/abandoned, same discard-by-generation-id approach onSearchScanReady uses.
            return;
        }

        m_searchSnapshotTags = result ? result.value() : TagsByPathMap{};
        m_searchResultsModel->setFileTags(m_searchSnapshotTags);
    });
}

void TabViewModel::requestAdvancedSearchResultTags(int generation, const std::vector<FileNode>& candidates)
{
    m_fileTagsService.requestTags(this, candidates, [this, generation](Result<TagsByPathMap> result) {
        if (generation != m_advancedSearchGeneration)
        {
            // Superseded/abandoned, same discard-by-generation-id approach
            // onAdvancedSearchCandidatesReady/onAdvancedSearchProgress use.
            return;
        }

        m_advancedSearchSnapshotTags = result ? result.value() : TagsByPathMap{};
        m_advancedSearchResultsModel->setFileTags(m_advancedSearchSnapshotTags);
    });
}

void TabViewModel::requestLocalBranches(std::function<void(Result<std::vector<std::string>>)> callback)
{
    if (!m_lastGitRepositorySummary.repositoryRoot)
    {
        callback(Result<std::vector<std::string>>::success({}));
        return;
    }

    const std::filesystem::path repositoryRoot = *m_lastGitRepositorySummary.repositoryRoot;
    m_gitStatusService.requestLocalBranches(this, repositoryRoot,
                                             [this, repositoryRoot, callback = std::move(callback)](Result<std::vector<std::string>> result) {
                                                 if (!m_lastGitRepositorySummary.repositoryRoot ||
                                                     *m_lastGitRepositorySummary.repositoryRoot != repositoryRoot)
                                                 {
                                                     // A newer navigation has since changed (or cleared) the repository; this reply is stale.
                                                     return;
                                                 }

                                                 callback(std::move(result));
                                             });
}

void TabViewModel::checkoutBranch(const QString& branchName)
{
    if (!m_lastGitRepositorySummary.repositoryRoot)
    {
        return;
    }

    if (!m_lastGitRepositorySummary.isDetached &&
        branchName.toStdString() == m_lastGitRepositorySummary.headDisplayName)
    {
        return;
    }

    m_gitStatusService.requestCheckout(this, *m_lastGitRepositorySummary.repositoryRoot, branchName.toStdString(),
                                        [this](Result<void> result) {
                                            if (!result)
                                            {
                                                emit gitOperationFailed(QString::fromStdString(result.error().message));
                                                return;
                                            }

                                            refresh();
                                        });
}

void TabViewModel::startMonitoring()
{
    if (m_monitoringActive || m_monitoringStarting)
    {
        return;
    }

    const std::filesystem::path root = currentPath();
    if (root.empty() || root == VirtualPaths::ThisPC || ArchiveExtensions::archiveAncestorInPath(root))
    {
        return;
    }

    m_monitoringLog.clear();
    m_monitoringTotal = 0;
    m_monitoringRoot = root;
    m_monitoringStarting = true;
    m_monitoringCancelled = false;

    // Opening the directory handle is disk I/O and now happens on IFolderMonitor's own worker
    // thread rather than synchronously here (Architecture.md's no-I/O-on-UI-thread rule) -- onStarted
    // reports the outcome once it's known. `guard` detects this tab being closed while the open is
    // still in flight; `folderMonitorUseCase` is captured separately (a plain pointer to a
    // CompositionRoot-owned, long-lived object) so the now-ownerless monitor can still be stopped
    // via it even if `this`/`guard` is already gone by the time the callback runs.
    QPointer<TabViewModel> guard(this);
    FolderMonitorUseCase* const folderMonitorUseCase = &m_folderMonitorUseCase;

    m_folderMonitorUseCase.startMonitoring(
        root,
        [this](const std::vector<FolderChangeEvent>& batch) {
            for (const FolderChangeEvent& event : batch)
            {
                if (m_monitoringLog.size() >= kMonitoringLogCap)
                {
                    m_monitoringLog.pop_front();
                }
                m_monitoringLog.push_back(event);
            }
            m_monitoringTotal += static_cast<int>(batch.size());

            emit monitoringLogAppended(batch);
            emit monitoringTotalChanged(m_monitoringTotal);
        },
        [this, guard, folderMonitorUseCase](Result<FolderMonitorId> result) {
            if (!guard)
            {
                // This tab was closed while the open was still in flight -- stop the now-ownerless
                // monitor immediately (if it actually succeeded) rather than leaking it for the
                // rest of the app's lifetime.
                if (result)
                {
                    folderMonitorUseCase->stopMonitoring(result.value());
                }
                return;
            }

            m_monitoringStarting = false;

            if (m_monitoringCancelled)
            {
                // stopMonitoring() ran while this was still in flight (there was no FolderMonitorId
                // yet to stop it by) -- honor that now instead of activating it.
                m_monitoringCancelled = false;
                if (result)
                {
                    folderMonitorUseCase->stopMonitoring(result.value());
                }
                return;
            }

            if (!result)
            {
                emit monitoringFailed(QString::fromStdString(result.error().message));
                return;
            }

            m_monitoringId = result.value();
            m_monitoringActive = true;
            emit monitoringActiveChanged(true);
        });
}

void TabViewModel::stopMonitoring()
{
    if (m_monitoringStarting)
    {
        // The open dispatched moments ago hasn't finished yet (Architecture.md's no-I/O-on-UI-thread
        // rule means that can take a while against an unresponsive network share) -- there is no
        // FolderMonitorId yet to stop it by. Flag it so startMonitoring()'s completion callback stops
        // it immediately once it does finish, instead of activating it.
        m_monitoringCancelled = true;
        return;
    }

    if (!m_monitoringActive)
    {
        return;
    }

    m_folderMonitorUseCase.stopMonitoring(m_monitoringId);
    m_monitoringActive = false;
    m_monitoringPinned = false;

    emit monitoringActiveChanged(false);
    emit monitoringPinnedChanged(false);
}

void TabViewModel::setMonitoringPinned(bool pinned)
{
    if (pinned == m_monitoringPinned || !m_monitoringActive)
    {
        return;
    }

    m_monitoringPinned = pinned;
    emit monitoringPinnedChanged(pinned);
}
