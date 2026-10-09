#include "TabViewModel.h"

#include <algorithm>
#include <utility>

#include <QPointer>

#include "AdvancedSearchService.h"
#include "ArchiveExtensions.h"
#include "DiskUsageNode.h"
#include "DiskUsageService.h"
#include "FileFragmentationInfo.h"
#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "FileTagsByPath.h"
#include "FileTagsService.h"
#include "FolderSizesByPath.h"
#include "FragmentCountsByPath.h"
#include "FragmentationService.h"
#include "GitDirectoryStatus.h"
#include "GitStatusService.h"
#include "IconLoadingService.h"
#include "MediaExtensions.h"
#include "MediaThumbnailLoadingService.h"
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
                           DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                           IconLoadingService& iconLoadingService, MediaThumbnailLoadingService& mediaThumbnailLoadingService,
                           TagChangeNotifier& tagChangeNotifier, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_gitStatusService(gitStatusService)
    , m_folderMonitorUseCase(folderMonitorUseCase)
    , m_advancedSearchService(advancedSearchService)
    , m_fileTagsService(fileTagsService)
    , m_diskUsageService(diskUsageService)
    , m_fragmentationService(fragmentationService)
    , m_iconLoadingService(iconLoadingService)
    , m_mediaThumbnailLoadingService(mediaThumbnailLoadingService)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_fileListModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_searchResultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_advancedSearchResultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_mediaViewerResultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
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
                requestFragmentCounts(path, entries);
                requestIcons(path, entries);
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
            const std::vector<FileNode>& candidates = m_searchFullScanDone ? m_searchSnapshot : m_searchPartialSnapshot;
            requestSearchResultTags(m_searchGeneration, candidates);
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

std::filesystem::path TabViewModel::expandEnvironmentVariables(const std::filesystem::path& input) const
{
    return m_fileNavigationUseCase.expandEnvironmentVariables(input);
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

    const IconSizeMask oldMask = iconSizeMaskFor(m_viewMode);
    const IconSizeMask newMask = iconSizeMaskFor(mode);

    m_viewMode = mode;
    emit viewModeChanged(mode);

    if (newMask == oldMask)
    {
        return;
    }

    requestIcons(currentPath(), m_fileListModel->entries());

    if (m_searchActive)
    {
        requestSearchResultIcons(m_searchGeneration, m_searchResultsModel->entries());
    }

    if (m_advancedSearchActive)
    {
        requestAdvancedSearchResultIcons(m_advancedSearchGeneration, m_advancedSearchResultsModel->entries());
    }
}

void TabViewModel::setSortCriterion(SortCriterion criterion, bool ascending)
{
    m_fileListModel->setSortCriterion(criterion, ascending);
}

void TabViewModel::setHiddenColumns(const std::array<std::optional<bool>, FileListModel::ColumnCount>& hiddenColumns)
{
    m_hiddenColumns = hiddenColumns;
}

void TabViewModel::setColumnHidden(FileListModel::Column column, bool hidden)
{
    m_hiddenColumns[static_cast<std::size_t>(column)] = hidden;
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

void TabViewModel::updateSearchQuery(const QString& query)
{
    if (query.trimmed().isEmpty())
    {
        exitSearch();
        return;
    }

    exitAdvancedSearch();

    m_searchQuery = query;
    runSearch(/*forceRescan=*/false);
}

void TabViewModel::refreshSearch(const QString& query)
{
    if (query.trimmed().isEmpty())
    {
        exitSearch();
        return;
    }

    exitAdvancedSearch();

    m_searchQuery = query;
    runSearch(/*forceRescan=*/true);
}

void TabViewModel::runSearch(bool forceRescan)
{
    if (forceRescan || m_searchRoot.empty() || (!m_searchFullScanDone && !m_searchScanPending))
    {
        // No scan covering the current root is running or cached -- dispatch a fresh one. This also
        // covers forceRescan explicitly re-scanning a root that *was* already fully/partially
        // scanned (Refresh clicked again).
        m_searchRoot = currentPath();
        m_searchFullScanDone = false;
        m_searchPartialSnapshot.clear();
        m_searchScanPending = true;

        const int generation = ++m_searchGeneration;

        emit searchLoading();
        m_advancedSearchService.requestRecursiveScan(
            this, m_searchRoot,
            [this, generation](Result<std::vector<FileNode>> result) { onSearchCandidatesReady(generation, std::move(result)); },
            [this, generation](std::vector<FileNode> partial) { onSearchProgress(generation, std::move(partial)); },
            m_searchSkipGitIgnored);
        return;
    }

    if (!m_searchFullScanDone && m_searchScanPending)
    {
        // A recursive scan of this same root is already running (the user kept typing while it's
        // in flight) -- don't start a second, redundant walk of the same subtree. Just re-filter
        // whatever the in-flight scan has reported so far; its own next progress tick (or final
        // result) will pick up m_searchQuery as it stands then (Architecture.md §14.37).
        applySearchFilter(m_searchPartialSnapshot);
        return;
    }

    // Cached, no rescan needed -- no disk I/O, so this stays fully synchronous (the common "user is
    // typing" case).
    applySearchFilter(m_searchSnapshot);
}

void TabViewModel::applySearchFilter(const std::vector<FileNode>& candidates)
{
    auto filtered = FileNavigationUseCase::filterByName(candidates, m_searchQuery.toStdString(), searchOptions());
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_searchResultsModel->setEntries(m_searchRoot, filtered);
    // setEntries() above clears the model's tags -- m_searchSnapshotTags already has every tag for
    // the current candidate list (requestSearchResultTags fetched it whenever that list itself last
    // changed), so re-applying it here needs no new DB round trip even though this runs on every
    // keystroke.
    m_searchResultsModel->setFileTags(m_searchSnapshotTags);

    if (!m_searchActive)
    {
        m_searchActive = true;
        setSelectedEntries({});
        emit searchModeChanged(true);
    }

    emit searchResultsChanged(filtered);
}

void TabViewModel::onSearchCandidatesReady(int generation, Result<std::vector<FileNode>> result)
{
    if (generation != m_searchGeneration)
    {
        // Superseded by a newer dispatch, or the search/tab was closed in the meantime -- discard.
        return;
    }

    m_searchScanPending = false;

    if (result.hasError())
    {
        emit searchFailed(QString::fromStdString(result.error().message));
        return;
    }

    m_searchSnapshot = std::move(result).value();
    m_searchPartialSnapshot.clear();
    m_searchFullScanDone = true;
    applySearchFilter(m_searchSnapshot);
    requestSearchResultTags(generation, m_searchSnapshot);
    requestSearchResultIcons(generation, m_searchSnapshot);

    emit searchLoadingFinished();
}

void TabViewModel::onSearchProgress(int generation, std::vector<FileNode> partial)
{
    if (generation != m_searchGeneration)
    {
        // Superseded/abandoned, same as onSearchCandidatesReady -- discard.
        return;
    }

    m_searchPartialSnapshot = std::move(partial);
    applySearchFilter(m_searchPartialSnapshot);
    requestSearchResultTags(generation, m_searchPartialSnapshot);
    requestSearchResultIcons(generation, m_searchPartialSnapshot);
}

void TabViewModel::setSearchOptions(const NameSearchOptions& options)
{
    const bool skipGitIgnoredChanged = options.skipGitIgnored != m_searchSkipGitIgnored;

    m_searchMatchMode = options.mode;
    m_searchCaseSensitive = options.caseSensitive;
    m_searchSkipGitIgnored = options.skipGitIgnored;

    if (m_searchQuery.isEmpty())
    {
        // Nothing to filter yet -- the new options still take effect the next time a query is
        // typed.
        return;
    }

    if (skipGitIgnoredChanged)
    {
        // Unlike mode/caseSensitive, this changes what a scan *visits*, not just how an
        // already-scanned candidate list is filtered -- a prior scan without pruning never
        // excluded anything, and a prior pruned scan never visited the excluded paths at all, so
        // neither cached snapshot can serve the other state from memory (Architecture.md §14.45).
        runSearch(/*forceRescan=*/true);
        return;
    }

    applySearchFilter(m_searchFullScanDone ? m_searchSnapshot : m_searchPartialSnapshot);
}

void TabViewModel::exitSearch()
{
    if (!m_searchActive)
    {
        return;
    }

    m_searchActive = false;
    m_searchSnapshot.clear();
    m_searchPartialSnapshot.clear();
    m_searchSnapshotTags.clear();
    m_searchQuery.clear();
    m_searchMatchMode = NameMatchMode::Contains;
    m_searchCaseSensitive = false;
    m_searchSkipGitIgnored = false;
    m_searchRoot.clear();
    m_searchFullScanDone = false;
    m_searchScanPending = false;
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
    showCurrentFolderNonRecursive();

    m_advancedSearchActive = true;
    setSelectedEntries({});
    emit advancedSearchModeChanged(true);
}

void TabViewModel::showCurrentFolderNonRecursive()
{
    // Not a real (recursive) search -- just a non-recursive listing of the current folder.
    const std::filesystem::path path = currentPath();
    auto result = m_fileNavigationUseCase.listDirectory(path);
    if (result.hasError())
    {
        emit advancedSearchFailed(QString::fromStdString(result.error().message));
        return;
    }

    // Discards any in-flight/cached scan -- mirrors exitAdvancedSearch's cleanup, but
    // m_advancedSearchActive/m_advancedSearchCriteria are left untouched by this function (the
    // pane stays open, and the criteria that led here, e.g. all-empty fields, is still whatever
    // the caller already set).
    m_advancedSearchSnapshot.clear();
    m_advancedSearchPartialSnapshot.clear();
    m_advancedSearchSnapshotTags.clear();
    m_advancedSearchRoot.clear();
    m_advancedSearchCachedTagIds.clear();
    m_advancedSearchFullScanDone = false;
    m_advancedSearchScanPending = false;
    ++m_advancedSearchGeneration;

    auto sorted = FileNavigationUseCase::sortBy(std::move(result).value(), SortCriterion::Name, true);
    m_advancedSearchResultsModel->setEntries(path, sorted);
    requestAdvancedSearchResultTags(m_advancedSearchGeneration, sorted);
    requestAdvancedSearchResultIcons(m_advancedSearchGeneration, sorted);

    emit advancedSearchResultsChanged(sorted);
    emit advancedSearchCriteriaChanged(m_advancedSearchCriteria);
}

void TabViewModel::startAdvancedSearch(const SearchCriteria& criteria)
{
    exitSearch();

    m_advancedSearchCriteria = criteria;

    if (isCriteriaEmpty(criteria))
    {
        if (!m_advancedSearchActive)
        {
            m_advancedSearchActive = true;
            setSelectedEntries({});
            emit advancedSearchModeChanged(true);
        }
        showCurrentFolderNonRecursive();
        return;
    }

    runAdvancedSearch(/*forceRescan=*/true);
}

void TabViewModel::updateAdvancedSearchCriteria(const SearchCriteria& criteria)
{
    if (!m_advancedSearchActive)
    {
        return;
    }

    const bool skipGitIgnoredChanged = criteria.nameOptions.skipGitIgnored != m_advancedSearchCriteria.nameOptions.skipGitIgnored;

    m_advancedSearchCriteria = criteria;

    if (isCriteriaEmpty(criteria))
    {
        showCurrentFolderNonRecursive();
        return;
    }

    runAdvancedSearch(/*forceRescan=*/skipGitIgnoredChanged);
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
        if (isCriteriaEmpty(m_advancedSearchCriteria))
        {
            showCurrentFolderNonRecursive();
        }
        else
        {
            runAdvancedSearch(/*forceRescan=*/false);
        }
    }
}

void TabViewModel::enterMediaViewer()
{
    if (m_mediaViewerActive)
    {
        return;
    }

    exitSearch();
    exitAdvancedSearch();

    m_mediaViewerRoot = currentPath();
    m_mediaViewerKind = MediaViewerKind::Image;
    m_mediaViewerTagIds.clear();
    m_mediaViewerNameQuery.clear();
    m_mediaViewerNameOptions = NameSearchOptions();
    m_mediaViewerCandidates.clear();

    m_mediaViewerActive = true;
    setSelectedEntries({});
    emit mediaViewerModeChanged(true);
    // The kind is force-reset to Image above without going through setMediaViewerKind (which
    // no-ops when the new kind already equals the old one) -- emit explicitly so
    // MediaViewerSearchPanelWidget's combo resyncs even when it was left showing Video from a
    // previous Media Viewer session on this tab.
    emit mediaViewerKindChanged(MediaViewerKind::Image);

    runMediaViewerQuery();
}

void TabViewModel::exitMediaViewer()
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    m_mediaViewerActive = false;
    m_mediaViewerRoot.clear();
    m_mediaViewerTagIds.clear();
    m_mediaViewerNameQuery.clear();
    m_mediaViewerNameOptions = NameSearchOptions();
    m_mediaViewerCandidates.clear();
    // Invalidates any fetch still in flight -- its (now-stale) result is discarded by
    // onMediaViewerCandidatesReady's generation check when it eventually arrives.
    ++m_mediaViewerGeneration;

    setSelectedEntries({});
    emit mediaViewerModeChanged(false);
}

void TabViewModel::setMediaViewerKind(MediaViewerKind kind)
{
    if (!m_mediaViewerActive || kind == m_mediaViewerKind)
    {
        return;
    }

    m_mediaViewerKind = kind;
    applyMediaViewerFilter();
    emit mediaViewerKindChanged(kind);
}

void TabViewModel::addMediaViewerTagFilter(Tag::Id tagId)
{
    if (!m_mediaViewerActive ||
        std::find(m_mediaViewerTagIds.begin(), m_mediaViewerTagIds.end(), tagId) != m_mediaViewerTagIds.end())
    {
        return;
    }

    m_mediaViewerTagIds.push_back(tagId);
    runMediaViewerQuery();
    emit mediaViewerCriteriaChanged();
}

void TabViewModel::removeMediaViewerTagFilter(Tag::Id tagId)
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    const auto it = std::find(m_mediaViewerTagIds.begin(), m_mediaViewerTagIds.end(), tagId);
    if (it == m_mediaViewerTagIds.end())
    {
        return;
    }

    m_mediaViewerTagIds.erase(it);
    runMediaViewerQuery();
    emit mediaViewerCriteriaChanged();
}

void TabViewModel::setMediaViewerNameFilter(const QString& query, const NameSearchOptions& options)
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    m_mediaViewerNameQuery = query;
    m_mediaViewerNameOptions = options;
    applyMediaViewerFilter();
}

void TabViewModel::runMediaViewerQuery()
{
    const int generation = ++m_mediaViewerGeneration;

    emit mediaViewerLoading();

    if (!m_mediaViewerTagIds.empty())
    {
        m_advancedSearchService.requestTagFirstCandidates(
            this, m_mediaViewerRoot, m_mediaViewerTagIds, [this, generation](Result<std::vector<FileNode>> result) {
                onMediaViewerCandidatesReady(generation, std::move(result));
            });
        return;
    }

    m_advancedSearchService.requestRecursiveScan(
        this, m_mediaViewerRoot,
        [this, generation](Result<std::vector<FileNode>> result) { onMediaViewerCandidatesReady(generation, std::move(result)); });
}

void TabViewModel::onMediaViewerCandidatesReady(int generation, Result<std::vector<FileNode>> result)
{
    if (generation != m_mediaViewerGeneration)
    {
        // Superseded by a newer request, or exitMediaViewer()/the tab was closed -- discard.
        return;
    }

    if (result.hasError())
    {
        emit mediaViewerFailed(QString::fromStdString(result.error().message));
        emit mediaViewerLoadingFinished();
        return;
    }

    m_mediaViewerCandidates = std::move(result).value();
    applyMediaViewerFilter();
    requestMediaViewerResultIcons(generation, m_mediaViewerCandidates);
    requestMediaViewerResultThumbnails(generation, m_mediaViewerCandidates);

    emit mediaViewerLoadingFinished();
}

void TabViewModel::applyMediaViewerFilter()
{
    std::vector<FileNode> filtered;
    filtered.reserve(m_mediaViewerCandidates.size());
    for (const FileNode& candidate : m_mediaViewerCandidates)
    {
        const std::string extension = MediaExtensions::lowercaseExtension(candidate.path());
        const bool matchesKind = m_mediaViewerKind == MediaViewerKind::Image ? MediaExtensions::isImageExtension(extension)
                                                                              : MediaExtensions::isVideoExtension(extension);
        if (matchesKind)
        {
            filtered.push_back(candidate);
        }
    }

    filtered = FileNavigationUseCase::filterByName(std::move(filtered), m_mediaViewerNameQuery.toStdString(), m_mediaViewerNameOptions);
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);

    m_mediaViewerResultsModel->setEntries(m_mediaViewerRoot, filtered);
    // setEntries() above clears the model's icon/thumbnail caches -- re-request both for the
    // filtered set (kind toggle, tag filter, or name query all funnel through here) so tiles don't
    // sit on the placeholder icon until the next full runMediaViewerQuery() rescan. Reuses the
    // current generation rather than bumping it: this is a re-filter of already-fetched candidates,
    // not a new fetch, so in-flight replies for this generation are still valid.
    requestMediaViewerResultIcons(m_mediaViewerGeneration, filtered);
    requestMediaViewerResultThumbnails(m_mediaViewerGeneration, filtered);
    emit mediaViewerResultsChanged(filtered);
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

std::vector<Tag> TabViewModel::resolveMediaViewerTagFilters() const
{
    auto result = m_tagManagementUseCase.allTags();
    if (result.hasError())
    {
        return {};
    }

    std::vector<Tag> resolved;
    for (const Tag& tag : result.value())
    {
        if (std::find(m_mediaViewerTagIds.begin(), m_mediaViewerTagIds.end(), tag.id()) != m_mediaViewerTagIds.end())
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
            [this, generation](std::vector<FileNode> partial) { onAdvancedSearchProgress(generation, std::move(partial)); },
            m_advancedSearchCriteria.nameOptions.skipGitIgnored);
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
        requestAdvancedSearchResultIcons(generation, m_advancedSearchSnapshot);
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
    requestAdvancedSearchResultIcons(generation, m_advancedSearchPartialSnapshot);
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

void TabViewModel::requestFragmentCounts(const std::filesystem::path& path, const std::vector<FileNode>& entries)
{
    const bool hasFiles = std::any_of(entries.begin(), entries.end(), [](const FileNode& entry) { return !entry.isDirectory(); });
    if (!hasFiles)
    {
        m_fileListModel->setFragmentCounts({});
        return;
    }

    m_fragmentationService.requestFilesUnder(this, path, [this, path](Result<std::vector<FileFragmentationInfo>> result) {
        if (path != currentPath())
        {
            // A newer navigation has since happened; this reply is stale.
            return;
        }

        FragmentCountsByPathMap counts;
        if (result)
        {
            for (const FileFragmentationInfo& info : result.value())
            {
                counts.emplace(info.path, info.fragmentCount);
            }
        }
        m_fileListModel->setFragmentCounts(counts);
    });
}

void TabViewModel::requestIcons(const std::filesystem::path& path, const std::vector<FileNode>& entries)
{
    m_iconLoadingService.requestIcons(m_fileListModel, path, entries, iconSizeMaskFor(m_viewMode), [this, path](QHash<QString, QIcon> batch) {
        if (path != currentPath())
        {
            // A newer navigation has since happened; this reply is stale.
            return;
        }

        m_fileListModel->setIcons(batch);
    });
}

void TabViewModel::requestSearchResultIcons(int generation, const std::vector<FileNode>& candidates)
{
    m_iconLoadingService.requestIcons(
        m_searchResultsModel, m_searchRoot, candidates, iconSizeMaskFor(m_viewMode), [this, generation](QHash<QString, QIcon> batch) {
            if (generation != m_searchGeneration)
            {
                // Superseded/abandoned, same discard-by-generation-id approach
                // onSearchCandidatesReady/onSearchProgress use.
                return;
            }

            m_searchResultsModel->setIcons(batch);
        });
}

void TabViewModel::requestAdvancedSearchResultIcons(int generation, const std::vector<FileNode>& candidates)
{
    m_iconLoadingService.requestIcons(
        m_advancedSearchResultsModel, m_advancedSearchRoot, candidates, iconSizeMaskFor(m_viewMode),
        [this, generation](QHash<QString, QIcon> batch) {
            if (generation != m_advancedSearchGeneration)
            {
                // Superseded/abandoned, same discard-by-generation-id approach
                // onAdvancedSearchCandidatesReady/onAdvancedSearchProgress use.
                return;
            }

            m_advancedSearchResultsModel->setIcons(batch);
        });
}

void TabViewModel::requestMediaViewerResultIcons(int generation, const std::vector<FileNode>& candidates)
{
    m_iconLoadingService.requestIcons(
        m_mediaViewerResultsModel, m_mediaViewerRoot, candidates, iconSizeMaskFor(ViewMode::ExtraLargeIcons),
        [this, generation](QHash<QString, QIcon> batch) {
            if (generation != m_mediaViewerGeneration)
            {
                // Superseded/abandoned, same discard-by-generation-id approach
                // onMediaViewerCandidatesReady uses.
                return;
            }

            m_mediaViewerResultsModel->setIcons(batch);
        });
}

void TabViewModel::requestMediaViewerResultThumbnails(int generation, const std::vector<FileNode>& candidates)
{
    // 256x256 matches FileBrowserView.cpp's iconSizeFor(ViewMode::ExtraLargeIcons) -- the fixed
    // grid sizing MediaViewerGrid mode locks to (Architecture.md §14.62).
    m_mediaThumbnailLoadingService.requestThumbnails(
        m_mediaViewerResultsModel, m_mediaViewerRoot, candidates, 256, 256,
        [this, generation](QHash<QString, MediaThumbnailData> batch) {
            if (generation != m_mediaViewerGeneration)
            {
                // Superseded/abandoned, same discard-by-generation-id approach
                // onMediaViewerCandidatesReady uses.
                return;
            }

            m_mediaViewerResultsModel->setMediaThumbnails(batch);
        });
}

void TabViewModel::requestSearchResultTags(int generation, const std::vector<FileNode>& candidates)
{
    m_fileTagsService.requestTags(this, candidates, [this, generation](Result<TagsByPathMap> result) {
        if (generation != m_searchGeneration)
        {
            // Superseded/abandoned, same discard-by-generation-id approach onSearchCandidatesReady/
            // onSearchProgress use.
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
