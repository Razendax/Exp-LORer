#include "TabViewModel.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "AdvancedSearchService.h"
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
#include "NotificationCenter.h"
#include "NotificationType.h"
#include "PathContainment.h"
#include "PathHash.h"
#include "PathUtf8.h"
#include "TagChangeNotifier.h"
#include "TagColorPalette.h"
#include "TagManagementUseCase.h"
#include "VirtualPaths.h"

namespace
{
    // Shared by filterMediaViewerCandidates() and requestKindSweep() (Architecture.md §14.80) so
    // the three buckets can't drift out of sync: Animation (GIF) is carved out of what would
    // otherwise be the Image bucket, so Image itself excludes it.
    bool mediaViewerKindMatches(const std::string& extension, MediaViewerKind kind)
    {
        switch (kind)
        {
        case MediaViewerKind::Image:
            return MediaExtensions::isImageExtension(extension) && !MediaExtensions::isAnimatedImageExtension(extension);
        case MediaViewerKind::Video:
            return MediaExtensions::isVideoExtension(extension);
        case MediaViewerKind::Animation:
            return MediaExtensions::isAnimatedImageExtension(extension);
        }
        return false;
    }

    bool isCriteriaEmpty(const SearchCriteria& criteria)
    {
        return criteria.nameQuery.empty() && !criteria.minSizeBytes && !criteria.maxSizeBytes && criteria.tagIds.empty();
    }
}

TabViewModel::TabViewModel(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                           const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                           GitStatusService& gitStatusService, FolderMonitorUseCase& folderMonitorUseCase,
                           AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                           DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                           IconLoadingService& iconLoadingService, MediaThumbnailLoadingService& mediaThumbnailLoadingService,
                           TagChangeNotifier& tagChangeNotifier, NotificationCenter& notificationCenter, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_gitStatusService(gitStatusService)
    , m_advancedSearchService(advancedSearchService)
    , m_fileTagsService(fileTagsService)
    , m_diskUsageService(diskUsageService)
    , m_fragmentationService(fragmentationService)
    , m_iconLoadingService(iconLoadingService)
    , m_mediaThumbnailLoadingService(mediaThumbnailLoadingService)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_notificationCenter(notificationCenter)
    , m_fileListModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_quickSearch(new TabQuickSearchViewModel(advancedSearchService, fileTagsService, iconLoadingService, tagChangeNotifier,
                                                 fileDecorationRules, fileDecorationsChangeSource, this))
    , m_advancedSearchResultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_mediaViewerResultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_monitoring(new TabMonitoringViewModel(folderMonitorUseCase, this))
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
                if (m_viewMode == ViewMode::ExtraLargeIcons)
                {
                    requestBrowsingThumbnails(path, entries);
                }
            });

    // Architecture.md §14.x: a tag assigned/removed/renamed/deleted anywhere re-fetches this tab's
    // currently-listed directory's tags, not just the path that actually changed -- correctness
    // over precision, since the alternative is threading which-path-changed information through a
    // signal meant to stay a plain broadcast. Also refreshes whichever of advanced search/Media
    // Viewer is currently active, for the same reason -- Quick Search reacts to the same event on
    // its own, via TabQuickSearchViewModel's own TagChangeNotifier connection.
    connect(&m_tagChangeNotifier, &TagChangeNotifier::tagsChanged, this, [this]() {
        requestFileTags(currentPath(), m_fileListModel->entries());

        if (m_advancedSearchActive)
        {
            const std::vector<FileNode>& candidates =
                m_advancedSearchFullScanDone ? m_advancedSearchSnapshot : m_advancedSearchPartialSnapshot;
            requestAdvancedSearchResultTags(m_advancedSearchGeneration, candidates);
        }

        if (m_mediaViewerActive)
        {
            requestMediaViewerResultTags(m_mediaViewerGeneration, m_mediaViewerResultsModel->entries());
        }
    });

    // Clearing the selection on a Quick Search activation/deactivation edge mirrors what
    // applyFilter()/exit() did inline before extraction (Architecture.md §14.96) -- kept here,
    // rather than giving TabQuickSearchViewModel a back-reference, since selection is Selection-
    // group (TabViewModel's own) state.
    connect(m_quickSearch, &TabQuickSearchViewModel::modeChanged, this, [this](bool) { setSelectedEntries({}); });

    // Entering Media Viewer mode's own progress-bar card (Architecture.md §14.65) -- only this
    // tab's own in-flight notification cares about a Cancel click, same filter-by-id posture
    // FragmentationViewModel/FolderSizeViewModel use.
    connect(&m_notificationCenter, &NotificationCenter::progressCancelRequested, this, [this](NotificationCenter::ProgressId id) {
        if (m_mediaViewerProgressId != 0 && id == m_mediaViewerProgressId)
        {
            exitMediaViewer();
        }
    });
}

TabViewModel::~TabViewModel()
{
    // Stopping an active monitor on destruction is now TabMonitoringViewModel's own responsibility
    // (its destructor guards this the same way, Architecture.md §14.96) -- m_monitoring is a child
    // QObject destroyed after this body runs.

    // Defensive guard -- exitMediaViewer()/WorkspacePaneViewModel::closeTab normally terminate this
    // first, but cover any other destruction path too (Architecture.md §14.65).
    if (m_mediaViewerProgressId != 0)
    {
        m_notificationCenter.terminateProgressNotification(m_mediaViewerProgressId);
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
    m_quickSearch->exit();
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
    m_quickSearch->exit();
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
    m_quickSearch->exit();
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
    m_quickSearch->exit();
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

    if (mode == ViewMode::ExtraLargeIcons)
    {
        // Architecture.md §14.71: every icon-grid tier resolves to the same IconSizeMask::Large, so
        // the oldMask == newMask early-return below never fires for a tier-to-tier switch -- without
        // this, switching between icon tiers would never (re)request thumbnails. A redundant
        // request for an already-decoded path is cheap (CachingMediaDecoder/ThumbnailCache short-
        // circuit on a cache hit), same cost profile as requestIcons() being re-called below.
        requestBrowsingThumbnails(currentPath(), m_fileListModel->entries());
    }

    if (newMask == oldMask)
    {
        return;
    }

    requestIcons(currentPath(), m_fileListModel->entries());
    m_quickSearch->setIconMask(newMask);

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
    if (!query.trimmed().isEmpty())
    {
        exitAdvancedSearch();
    }

    m_quickSearch->updateQuery(query, currentPath());
}

void TabViewModel::refreshSearch(const QString& query)
{
    if (!query.trimmed().isEmpty())
    {
        exitAdvancedSearch();
    }

    m_quickSearch->refresh(query, currentPath());
}

void TabViewModel::showAdvancedSearchPanel()
{
    if (m_advancedSearchActive)
    {
        return;
    }

    m_quickSearch->exit();
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
    m_advancedSearchSnapshotIcons.clear();
    m_advancedSearchRoot.clear();
    m_advancedSearchCachedTagIds.clear();
    m_advancedSearchCachedTagIncludeChildren = false;
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
    m_quickSearch->exit();

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
    const bool tagIncludeChildTagsChanged = criteria.tagIncludeChildTags != m_advancedSearchCriteria.tagIncludeChildTags;

    m_advancedSearchCriteria = criteria;

    if (isCriteriaEmpty(criteria))
    {
        showCurrentFolderNonRecursive();
        return;
    }

    runAdvancedSearch(/*forceRescan=*/skipGitIgnoredChanged || tagIncludeChildTagsChanged);
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
    m_advancedSearchSnapshotIcons.clear();
    m_advancedSearchCriteria = SearchCriteria();
    m_advancedSearchRoot.clear();
    m_advancedSearchCachedTagIds.clear();
    m_advancedSearchCachedTagIncludeChildren = false;
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
        m_quickSearch->exit();
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

    m_quickSearch->exit();
    exitAdvancedSearch();

    m_mediaViewerRoot = currentPath();
    m_mediaViewerKind = MediaViewerKind::Image;
    m_mediaViewerTagIds.clear();
    m_mediaViewerNameQuery.clear();
    m_mediaViewerNameOptions = NameSearchOptions();
    m_mediaViewerCandidates.clear();

    // Folder-Grouped Browsing's own navigation state (Architecture.md §14.74) -- reset
    // unconditionally regardless of m_mediaViewerGroupMode's current value (a toolbar preference
    // that persists across sessions), so a session left browsing a deep subfolder last time starts
    // back at the root this time.
    m_mediaViewerCurrentFolder = m_mediaViewerRoot;
    m_mediaViewerFolderHistory = NavigationHistory();
    m_mediaViewerFolderHistory.navigate(m_mediaViewerRoot);

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
    m_mediaViewerCurrentFolder.clear();
    m_mediaViewerFolderHistory = NavigationHistory();
    // Invalidates any fetch still in flight -- its (now-stale) result is discarded by
    // onMediaViewerCandidatesReady's generation check when it eventually arrives.
    ++m_mediaViewerGeneration;

    // Covers both the notification's own Cancel button and the user manually toggling Media
    // Viewer mode off mid-load (Architecture.md §14.65) -- "discard, not abort": any in-flight
    // scan/decode batch for the now-stale generation is simply ignored when it lands.
    if (m_mediaViewerProgressId != 0)
    {
        m_notificationCenter.terminateProgressNotification(m_mediaViewerProgressId);
        m_mediaViewerProgressId = 0;
    }
    m_mediaViewerThumbnailsTotal = 0;
    m_mediaViewerThumbnailsDone = 0;
    m_mediaViewerCountedPaths.clear();
    m_mediaViewerImageSweepRequested = false;
    m_mediaViewerVideoSweepRequested = false;
    m_mediaViewerAnimationSweepRequested = false;

    setSelectedEntries({});
    emit mediaViewerModeChanged(false);
}

void TabViewModel::renameMediaViewerEntry(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    bool changed = false;
    for (FileNode& candidate : m_mediaViewerCandidates)
    {
        if (isPathWithinOrEqual(candidate.path(), source))
        {
            candidate = candidate.withPath(rebasePath(candidate.path(), source, destination));
            changed = true;
        }
    }

    if (!changed)
    {
        return;
    }

    if (isPathWithinOrEqual(m_mediaViewerCurrentFolder, source))
    {
        m_mediaViewerCurrentFolder = rebasePath(m_mediaViewerCurrentFolder, source, destination);
    }

    applyMediaViewerFilter(false);
}

void TabViewModel::setMediaViewerKind(MediaViewerKind kind)
{
    if (!m_mediaViewerActive || kind == m_mediaViewerKind)
    {
        return;
    }

    m_mediaViewerKind = kind;
    applyMediaViewerFilter(false);
    // Switching to a kind nobody has looked at yet this generation actually has to decode it
    // (Architecture.md §14.66) -- requestKindSweep no-ops if `kind`'s sweep was already dispatched
    // (e.g. switching back to a kind seen earlier this generation is a cheap re-filter only).
    requestKindSweep(m_mediaViewerGeneration, kind);
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
    applyMediaViewerFilter(false);
}

void TabViewModel::setMediaViewerSortCriterion(MediaViewerSortCriterion criterion)
{
    if (criterion == m_mediaViewerSortCriterion)
    {
        return;
    }

    m_mediaViewerSortCriterion = criterion;
    emit mediaViewerSortCriterionChanged(criterion);
    reapplyMediaViewerSort();
}

void TabViewModel::setMediaViewerSortAscending(bool ascending)
{
    if (ascending == m_mediaViewerSortAscending)
    {
        return;
    }

    m_mediaViewerSortAscending = ascending;
    emit mediaViewerSortAscendingChanged(ascending);
    reapplyMediaViewerSort();
}

void TabViewModel::setMediaViewerViewMode(MediaViewerViewMode mode)
{
    if (mode == m_mediaViewerViewMode)
    {
        return;
    }

    m_mediaViewerViewMode = mode;
    emit mediaViewerViewModeChanged(mode);
    reapplyMediaViewerSort();
}

void TabViewModel::setMediaViewerGroupMode(MediaViewerGroupMode mode)
{
    if (!m_mediaViewerActive || mode == m_mediaViewerGroupMode)
    {
        return;
    }

    m_mediaViewerGroupMode = mode;

    // Neither direction touches mediaViewerCurrentFolder()/the folder-nav history -- switching
    // Recursive<->ByFolder preserves wherever Folder-mode browsing currently is (Architecture.md
    // §14.74), rather than snapping Folder mode back to the root every time it's re-selected.

    applyMediaViewerFilter(false);
    emit mediaViewerGroupModeChanged(mode);
}

void TabViewModel::setMediaViewerPreviewMode(MediaViewerPreviewMode mode)
{
    if (!m_mediaViewerActive || mode == m_mediaViewerPreviewMode)
    {
        return;
    }

    m_mediaViewerPreviewMode = mode;
    emit mediaViewerPreviewModeChanged(mode);
}

std::filesystem::path TabViewModel::mediaViewerUpTarget() const
{
    if (pathsEqualCaseInsensitive(m_mediaViewerCurrentFolder, m_mediaViewerRoot))
    {
        // No legal parent within the confined subtree -- returning the current folder itself
        // signals "nothing to navigate to", mirroring upTarget()'s ThisPC clamp.
        return m_mediaViewerCurrentFolder;
    }

    return m_mediaViewerCurrentFolder.parent_path();
}

std::optional<std::filesystem::path> TabViewModel::mediaViewerPeekBack() const
{
    return m_mediaViewerFolderHistory.peekBack();
}

std::optional<std::filesystem::path> TabViewModel::mediaViewerPeekForward() const
{
    return m_mediaViewerFolderHistory.peekForward();
}

void TabViewModel::mediaViewerNavigateTo(const std::filesystem::path& path)
{
    if (!m_mediaViewerActive || !isPathWithinOrEqual(path, m_mediaViewerRoot))
    {
        return;
    }

    m_mediaViewerFolderHistory.navigate(path);
    m_mediaViewerCurrentFolder = path;
    applyMediaViewerFilter(false);

    emit mediaViewerCurrentFolderChanged(m_mediaViewerCurrentFolder);
    emitMediaViewerFolderAvailability();
}

void TabViewModel::mediaViewerGoUp()
{
    const std::filesystem::path target = mediaViewerUpTarget();
    if (target == m_mediaViewerCurrentFolder)
    {
        return;
    }

    mediaViewerNavigateTo(target);
}

void TabViewModel::mediaViewerGoBack()
{
    const auto path = m_mediaViewerFolderHistory.goBack();
    if (!path)
    {
        return;
    }

    m_mediaViewerCurrentFolder = *path;
    applyMediaViewerFilter(false);

    emit mediaViewerCurrentFolderChanged(m_mediaViewerCurrentFolder);
    emitMediaViewerFolderAvailability();
}

void TabViewModel::mediaViewerGoForward()
{
    const auto path = m_mediaViewerFolderHistory.goForward();
    if (!path)
    {
        return;
    }

    m_mediaViewerCurrentFolder = *path;
    applyMediaViewerFilter(false);

    emit mediaViewerCurrentFolderChanged(m_mediaViewerCurrentFolder);
    emitMediaViewerFolderAvailability();
}

void TabViewModel::setMediaViewerTagBehaviour(MediaViewerTagBehaviour behaviour)
{
    if (behaviour == m_mediaViewerTagBehaviour)
    {
        return;
    }

    m_mediaViewerTagBehaviour = behaviour;
    emit mediaViewerTagBehaviourChanged(behaviour);
}

void TabViewModel::handleMediaViewerSearchTagActivated(Tag::Id tagId, bool ctrlHeld)
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    if ((m_mediaViewerTagBehaviour == MediaViewerTagBehaviour::Filter) != ctrlHeld)
    {
        addMediaViewerTagFilter(tagId);
    }
    else
    {
        applyMediaViewerTagToSelection(tagId);
    }
}

void TabViewModel::applyMediaViewerTagToSelection(Tag::Id tagId)
{
    if (m_selectedEntries.empty())
    {
        emit mediaViewerFailed(tr("No items selected to apply the tag to."));
        return;
    }

    std::size_t successCount = 0;
    for (const FileNode& entry : m_selectedEntries)
    {
        auto result = m_tagManagementUseCase.assignTag(entry, tagId);
        if (result || result.error().code == ErrorCode::AlreadyExists)
        {
            ++successCount;
        }
    }

    if (successCount == 0)
    {
        return;
    }

    m_notificationCenter.showPopup(NotificationType::Info, tr("Tag Applied"),
                                    tr("Tagged %1 item(s).").arg(successCount));
    emit m_tagChangeNotifier.tagsChanged();
}

void TabViewModel::acceptMediaViewerSearchTagQuery(const QString& query)
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    const auto results = searchTagsForCriteria(trimmed);
    const auto it = std::find_if(results.begin(), results.end(), [&trimmed](const Tag& tag) {
        return QString::fromStdString(tag.name()).compare(trimmed, Qt::CaseInsensitive) == 0;
    });
    if (it != results.end())
    {
        applyMediaViewerTagToSelection(it->id());
    }
    else
    {
        createAndApplyMediaViewerTag(trimmed);
    }
}

void TabViewModel::createAndApplyMediaViewerTag(const QString& name)
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    const std::string colorHex = TagColorPalette::pickColorFor(trimmed.toStdString());
    auto created = m_tagManagementUseCase.createTag(trimmed.toStdString(), colorHex);
    if (!created)
    {
        emit mediaViewerFailed(QString::fromStdString(created.error().message));
        return;
    }

    applyMediaViewerTagToSelection(created.value().id());
}

void TabViewModel::handleMediaViewerDetailsTagClick(const FileNode& entry, Tag::Id tagId, bool ctrlHeld)
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    if ((m_mediaViewerTagBehaviour == MediaViewerTagBehaviour::Filter) != ctrlHeld)
    {
        addMediaViewerTagFilter(tagId);
        return;
    }

    auto result = m_tagManagementUseCase.unassignTag(entry, tagId);
    if (!result)
    {
        emit mediaViewerFailed(QString::fromStdString(result.error().message));
        return;
    }
    emit m_tagChangeNotifier.tagsChanged();
}

void TabViewModel::runMediaViewerQuery()
{
    const int generation = ++m_mediaViewerGeneration;

    emit mediaViewerLoading();

    // A tag-filter change re-querying while a previous scan/decode is still in flight terminates
    // that notification first, rather than leaving it to onMediaViewerCandidatesReady/the
    // destructor -- there would otherwise be a window where a stale bar from the superseded
    // generation is still visible (Architecture.md §14.65).
    if (m_mediaViewerProgressId != 0)
    {
        m_notificationCenter.terminateProgressNotification(m_mediaViewerProgressId);
    }
    m_mediaViewerThumbnailsTotal = 0;
    m_mediaViewerThumbnailsDone = 0;
    m_mediaViewerCountedPaths.clear();
    m_mediaViewerProgressId = m_notificationCenter.createProgressNotification(tr("Loading Media Viewer..."), /*barCount=*/1,
                                                                                /*cancellable=*/true);
    m_notificationCenter.updateProgressBar(m_mediaViewerProgressId, 0, 0, 0, tr("Scanning folder..."), QString(), QString());

    if (!m_mediaViewerTagIds.empty())
    {
        // No onProgress hook on this (typically much faster, DB-driven) path -- it keeps the
        // static "Scanning folder..." text set above.
        m_advancedSearchService.requestTagFirstCandidates(
            this, m_mediaViewerRoot, m_mediaViewerTagIds, [this, generation](Result<std::vector<FileNode>> result) {
                onMediaViewerCandidatesReady(generation, std::move(result));
            });
        return;
    }

    m_advancedSearchService.requestRecursiveScan(
        this, m_mediaViewerRoot,
        [this, generation](Result<std::vector<FileNode>> result) { onMediaViewerCandidatesReady(generation, std::move(result)); },
        [this, generation](std::vector<FileNode> partial) {
            if (generation != m_mediaViewerGeneration)
            {
                return;
            }

            m_notificationCenter.updateProgressBar(m_mediaViewerProgressId, 0, 0, 0,
                                                     tr("Scanning: %1 file(s) found").arg(partial.size()), QString(), QString());
        });
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
        if (m_mediaViewerProgressId != 0)
        {
            m_notificationCenter.terminateProgressNotification(m_mediaViewerProgressId);
            m_mediaViewerProgressId = 0;
        }
        emit mediaViewerFailed(QString::fromStdString(result.error().message));
        emit mediaViewerLoadingFinished();
        return;
    }

    m_mediaViewerCandidates = std::move(result).value();
    // Fresh candidate set -- none of the three kinds has had its background sweep dispatched yet
    // (Architecture.md §14.66/§14.80).
    m_mediaViewerImageSweepRequested = false;
    m_mediaViewerVideoSweepRequested = false;
    m_mediaViewerAnimationSweepRequested = false;

    // Fresh candidate set from a new recursive/tag-first scan (possibly a different root than
    // whatever this tab's media viewer last showed) -- unlike a plain kind/name re-filter of
    // candidates already held, stale cached icons/thumbnails from the previous set are not worth
    // keeping around, so let applyMediaViewerFilter() clear them via setEntries().
    applyMediaViewerFilter(true);
    // Only the currently-selected kind is decoded eagerly (Architecture.md §14.66) -- the other
    // kind, if any, is only decoded if/when the dropdown switches to it (setMediaViewerKind).
    requestKindSweep(generation, m_mediaViewerKind);

    emit mediaViewerLoadingFinished();
}

std::vector<FileNode> TabViewModel::filterMediaViewerCandidates() const
{
    std::vector<FileNode> filtered;
    filtered.reserve(m_mediaViewerCandidates.size());
    for (const FileNode& candidate : m_mediaViewerCandidates)
    {
        // Scoped to mediaViewerCurrentFolder() rather than m_mediaViewerRoot (Architecture.md
        // §14.74): equal to m_mediaViewerRoot whenever Folder mode hasn't navigated anywhere, so
        // this is a no-op for the common case, but lets switching from ByFolder back to Recursive
        // show a recursive gallery of wherever Folder-mode browsing currently is instead of
        // snapping back out to the full original root.
        if (!isPathWithinOrEqual(candidate.path(), m_mediaViewerCurrentFolder))
        {
            continue;
        }

        const std::string extension = MediaExtensions::lowercaseExtension(candidate.path());
        if (mediaViewerKindMatches(extension, m_mediaViewerKind))
        {
            filtered.push_back(candidate);
        }
    }

    return FileNavigationUseCase::filterByName(std::move(filtered), m_mediaViewerNameQuery.toStdString(), m_mediaViewerNameOptions);
}

std::vector<FileNode> TabViewModel::sortMediaViewerResults(std::vector<FileNode> filtered) const
{
    switch (m_mediaViewerSortCriterion)
    {
    case MediaViewerSortCriterion::Name:
        return FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, m_mediaViewerSortAscending);
    case MediaViewerSortCriterion::Size:
        return FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Size, m_mediaViewerSortAscending);
    case MediaViewerSortCriterion::Duration:
        std::stable_sort(filtered.begin(), filtered.end(), [this](const FileNode& a, const FileNode& b) {
            const auto da = m_mediaViewerResultsModel->mediaThumbnailDuration(QString::fromStdWString(a.path().wstring()));
            const auto db = m_mediaViewerResultsModel->mediaThumbnailDuration(QString::fromStdWString(b.path().wstring()));
            if (da.has_value() != db.has_value())
            {
                // Unknown duration always sorts last, regardless of ascending/descending.
                return da.has_value();
            }
            if (!da.has_value())
            {
                return false;
            }
            return m_mediaViewerSortAscending ? *da < *db : *da > *db;
        });
        return filtered;
    }

    return filtered;
}

TabViewModel::FolderGroupedView TabViewModel::buildFolderGroupedView(const std::vector<FileNode>& filtered) const
{
    FolderGroupedView view;

    // First pass: partition `filtered` into direct children of m_mediaViewerCurrentFolder (kept
    // as-is) and everything else still under it, bucketed by immediate child-of-current-folder
    // ancestor directory.
    std::vector<std::filesystem::path> bucketOrder; // first-seen order, for deterministic iteration
    std::unordered_map<std::filesystem::path, std::vector<FileNode>, PathHash> buckets;

    for (const FileNode& candidate : filtered)
    {
        if (!isPathWithinOrEqual(candidate.path(), m_mediaViewerCurrentFolder))
        {
            continue;
        }

        const std::filesystem::path parent = candidate.path().parent_path();
        if (pathsEqualCaseInsensitive(parent, m_mediaViewerCurrentFolder))
        {
            view.combined.push_back(candidate);
            view.directChildFiles.push_back(candidate);
            continue;
        }

        // Climb from the candidate's parent until reaching the ancestor directory whose own
        // parent is m_mediaViewerCurrentFolder -- that directory is the bucket key.
        std::filesystem::path bucketKey = parent;
        for (;;)
        {
            const std::filesystem::path bucketParent = bucketKey.parent_path();
            if (pathsEqualCaseInsensitive(bucketParent, m_mediaViewerCurrentFolder) || bucketParent == bucketKey)
            {
                break;
            }
            bucketKey = bucketParent;
        }

        auto [it, inserted] = buckets.try_emplace(bucketKey);
        if (inserted)
        {
            bucketOrder.push_back(bucketKey);
        }
        it->second.push_back(candidate);
    }

    // Second pass: synthesize one directory FileNode + preview seed per non-empty bucket, sampling
    // from each bucket's own entries in the current sort order (Architecture.md §14.74).
    for (const std::filesystem::path& bucketKey : bucketOrder)
    {
        auto node = FileNode::create(bucketKey, 0, std::chrono::system_clock::now(), std::chrono::system_clock::now(),
                                      FileType::Directory);
        if (!node)
        {
            continue;
        }

        const std::vector<FileNode> bucketEntries = sortMediaViewerResults(buckets.at(bucketKey));

        FolderPreviewSeed seed;
        seed.itemCount = static_cast<int>(bucketEntries.size());
        const std::size_t sampleCount = std::min<std::size_t>(4, bucketEntries.size());
        seed.samplePaths.reserve(sampleCount);
        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            seed.samplePaths.push_back(QString::fromStdWString(bucketEntries[i].path().wstring()));
            view.previewSampleFiles.push_back(bucketEntries[i]);
        }

        view.previewsByPath.insert(QString::fromStdWString(bucketKey.wstring()), seed);
        view.combined.push_back(node.value());
    }

    return view;
}

TabViewModel::MediaViewerDisplay TabViewModel::buildMediaViewerDisplay() const
{
    MediaViewerDisplay display;
    std::vector<FileNode> kindNameFiltered = filterMediaViewerCandidates();

    display.grouped = m_mediaViewerGroupMode == MediaViewerGroupMode::ByFolder && m_mediaViewerViewMode == MediaViewerViewMode::Thumbnails;

    if (!display.grouped)
    {
        display.entries = sortMediaViewerResults(std::move(kindNameFiltered));
        display.dispatchTargets = display.entries;
        return display;
    }

    FolderGroupedView grouped = buildFolderGroupedView(kindNameFiltered);
    display.entries = sortMediaViewerResults(std::move(grouped.combined));
    display.dispatchTargets = std::move(grouped.directChildFiles);
    // Folder tiles' collage samples need their own icon/thumbnail/tag request too (Architecture.md
    // §14.74) -- otherwise they'd only ever fill in once requestKindSweep's whole-tree Low-priority
    // background sweep happens to reach them, which can take a long time in a large tree even
    // though only a handful of samples are actually on screen right now.
    display.dispatchTargets.insert(display.dispatchTargets.end(), grouped.previewSampleFiles.begin(), grouped.previewSampleFiles.end());
    display.folderPreviews = std::move(grouped.previewsByPath);
    return display;
}

void TabViewModel::reapplyMediaViewerSort()
{
    if (!m_mediaViewerActive)
    {
        return;
    }

    const MediaViewerDisplay display = buildMediaViewerDisplay();
    m_mediaViewerResultsModel->refilterEntries(display.entries, /*preSorted=*/true);
    if (display.grouped)
    {
        m_mediaViewerResultsModel->setFolderPreviews(display.folderPreviews);
    }
}

void TabViewModel::applyMediaViewerFilter(bool freshCandidates)
{
    const MediaViewerDisplay display = buildMediaViewerDisplay();

    if (freshCandidates)
    {
        m_mediaViewerResultsModel->setEntriesPreSorted(display.grouped ? m_mediaViewerCurrentFolder : m_mediaViewerRoot, display.entries);
    }
    else
    {
        // Image<->Video kind toggle, a name-query keystroke, or a Folder-mode regroup: narrowing/
        // regrouping the SAME already-fetched candidate set, not a new one, so this goes through
        // refilterEntries() instead of setEntries() -- it leaves the model's icon/thumbnail caches
        // alone, so a tile that drops out of view and later comes back (toggling the kind filter
        // back, say) stays showing its already-decoded thumbnail instead of flashing back to the
        // placeholder icon while MediaThumbnailLoadingService redundantly redecodes something it
        // already decoded (Architecture.md §14.62).
        m_mediaViewerResultsModel->refilterEntries(display.entries, /*preSorted=*/true);
    }

    if (display.grouped)
    {
        m_mediaViewerResultsModel->setFolderPreviews(display.folderPreviews);
    }

    // Reuses the current generation rather than bumping it: this is a re-filter/fresh-listing
    // handled above, not itself a new fetch, so in-flight replies for this generation are still
    // valid. Still needed even when refilterEntries() preserved the cache: entries that are newly
    // visible after narrowing/widening the filter may not have been requested before. Scoped to
    // display.dispatchTargets rather than display.entries when grouped (Architecture.md §14.74):
    // synthesized subfolder rows never need their own icon/thumbnail/tag request, since their
    // collage is drawn entirely from already-requested sample children.
    requestMediaViewerResultIcons(m_mediaViewerGeneration, display.dispatchTargets);
    // High priority (Architecture.md §14.66): `display.dispatchTargets` is the kind+name match on
    // screen right now, so it jumps ahead of a kind's background sweep that may already be mid-
    // flight. Its completions still count toward the active progress notification (de-duplicated by
    // path against the sweep's own total) -- see requestMediaViewerResultThumbnails's doc comment.
    requestMediaViewerResultThumbnails(m_mediaViewerGeneration, display.dispatchTargets, MediaThumbnailLoadingService::Priority::High);
    // Details view's tag row (Architecture.md §14.69) -- harmless to fetch even while another
    // view mode is active, mirroring requestMediaViewerResultIcons/Thumbnails above.
    requestMediaViewerResultTags(m_mediaViewerGeneration, display.dispatchTargets);
    emit mediaViewerResultsChanged(display.entries);
}

void TabViewModel::requestKindSweep(int generation, MediaViewerKind kind)
{
    bool* sweepRequested = &m_mediaViewerImageSweepRequested;
    if (kind == MediaViewerKind::Video)
    {
        sweepRequested = &m_mediaViewerVideoSweepRequested;
    }
    else if (kind == MediaViewerKind::Animation)
    {
        sweepRequested = &m_mediaViewerAnimationSweepRequested;
    }
    if (*sweepRequested)
    {
        return;
    }
    *sweepRequested = true;

    // Ignores the name filter -- this is the whole-kind background sweep, so clearing the name
    // filter later never has to wait on a fresh decode pass.
    std::vector<FileNode> kindCandidates;
    kindCandidates.reserve(m_mediaViewerCandidates.size());
    for (const FileNode& candidate : m_mediaViewerCandidates)
    {
        const std::string extension = MediaExtensions::lowercaseExtension(candidate.path());
        if (mediaViewerKindMatches(extension, kind))
        {
            kindCandidates.push_back(candidate);
        }
    }

    m_mediaViewerThumbnailsTotal = kindCandidates.size();
    m_mediaViewerThumbnailsDone = 0;
    m_mediaViewerCountedPaths.clear();

    if (m_mediaViewerThumbnailsTotal == 0)
    {
        // Nothing to decode for this kind -- terminate now rather than leaving a determinate 0/0
        // bar up until the next mode change.
        if (m_mediaViewerProgressId != 0)
        {
            m_notificationCenter.terminateProgressNotification(m_mediaViewerProgressId);
            m_mediaViewerProgressId = 0;
        }
    }
    else
    {
        // Reuses an already-open notification in place (the "Scanning folder..." bar
        // runMediaViewerQuery() just created, for the first sweep of a fresh fetch) rather than
        // terminating and recreating it; otherwise (switching to a kind whose own sweep hasn't run
        // yet this generation, well after the fetch's own bar already closed) opens a fresh one --
        // this is what makes that switch visibly show loading progress (Architecture.md §14.66).
        if (m_mediaViewerProgressId == 0)
        {
            m_mediaViewerProgressId =
                m_notificationCenter.createProgressNotification(tr("Loading Media Viewer..."), /*barCount=*/1, /*cancellable=*/true);
        }
        m_notificationCenter.updateProgressBar(m_mediaViewerProgressId, 0, 0, static_cast<int>(m_mediaViewerThumbnailsTotal),
                                                 tr("Loading %1 item(s)...").arg(m_mediaViewerThumbnailsTotal),
                                                 tr("0 of %1").arg(m_mediaViewerThumbnailsTotal), QString());
    }

    // Icons for non-media files are never shown in the grid, so this (like the thumbnail request
    // right after) is scoped to kindCandidates rather than every file in the tree.
    requestMediaViewerResultIcons(generation, kindCandidates);
    // Low priority (Architecture.md §14.66): this is the whole-kind background sweep, so it never
    // blocks whatever currently matches the visible filter (dispatched High by
    // applyMediaViewerFilter).
    requestMediaViewerResultThumbnails(generation, kindCandidates, MediaThumbnailLoadingService::Priority::Low);
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
        const bool includeChildren = m_advancedSearchCriteria.tagIncludeChildTags;

        if (!forceRescan && m_advancedSearchCachedTagIds == tagIds &&
            m_advancedSearchCachedTagIncludeChildren == includeChildren &&
            (m_advancedSearchFullScanDone || m_advancedSearchScanPending))
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
        m_advancedSearchCachedTagIncludeChildren = includeChildren;
        m_advancedSearchFullScanDone = false;
        m_advancedSearchScanPending = true;
        m_advancedSearchPartialSnapshot.clear();
        m_advancedSearchSnapshotIcons.clear();

        const int generation = ++m_advancedSearchGeneration;

        emit advancedSearchLoading();
        auto onCandidatesReady = [this, generation](Result<std::vector<FileNode>> result) {
            onAdvancedSearchCandidatesReady(generation, std::move(result), /*isNewSnapshot=*/true);
        };
        if (includeChildren)
        {
            m_advancedSearchService.requestTagFirstCandidatesIncludingChildren(this, m_advancedSearchRoot, tagIds,
                                                                                 std::move(onCandidatesReady));
        }
        else
        {
            m_advancedSearchService.requestTagFirstCandidates(this, m_advancedSearchRoot, tagIds,
                                                                std::move(onCandidatesReady));
        }
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
        m_advancedSearchCachedTagIncludeChildren = false;
        m_advancedSearchFullScanDone = false;
        m_advancedSearchPartialSnapshot.clear();
        m_advancedSearchSnapshotIcons.clear();
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
    // setEntries() also clears the model's own icon cache -- same reapply, from
    // m_advancedSearchSnapshotIcons, so rows whose icon IconLoadingService already resolved don't
    // flash back to the placeholder on every criteria edit (see TabQuickSearchViewModel's
    // m_snapshotIcons for the mirror of this).
    m_advancedSearchResultsModel->setIcons(m_advancedSearchSnapshotIcons);

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

void TabViewModel::emitMediaViewerFolderAvailability()
{
    emit mediaViewerBackAvailableChanged(m_mediaViewerFolderHistory.canGoBack());
    emit mediaViewerForwardAvailableChanged(m_mediaViewerFolderHistory.canGoForward());
    emit mediaViewerUpAvailableChanged(mediaViewerUpTarget() != m_mediaViewerCurrentFolder);
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

void TabViewModel::requestBrowsingThumbnails(const std::filesystem::path& path, const std::vector<FileNode>& entries)
{
    std::vector<FileNode> mediaEntries;
    for (const FileNode& entry : entries)
    {
        if (entry.isDirectory())
        {
            continue;
        }

        const std::string extension = MediaExtensions::lowercaseExtension(entry.path());
        if (MediaExtensions::isImageExtension(extension) || MediaExtensions::isVideoExtension(extension))
        {
            mediaEntries.push_back(entry);
        }
    }

    if (mediaEntries.empty())
    {
        return;
    }

    // 256x256 matches FileBrowserView.cpp's iconSizeFor(ViewMode::ExtraLargeIcons).
    m_mediaThumbnailLoadingService.requestThumbnails(
        m_fileListModel, path, std::move(mediaEntries), 256, 256,
        [this, path](QHash<QString, MediaThumbnailData> batch) {
            if (path != currentPath())
            {
                // A newer navigation has since happened; this reply is stale.
                return;
            }

            m_fileListModel->setMediaThumbnails(batch);
        },
        MediaThumbnailLoadingService::Priority::High);
}

void TabViewModel::requestHoverPreviewThumbnail(const FileNode& entry, int maxWidth, int maxHeight,
                                                 std::function<void(QImage)> onReady)
{
    const std::filesystem::path path = entry.path();
    m_mediaThumbnailLoadingService.requestThumbnails(
        this, path, { entry }, maxWidth, maxHeight,
        [path, onReady = std::move(onReady)](QHash<QString, MediaThumbnailData> batch) {
            const auto it = batch.find(QString::fromStdString(PathUtf8::toUtf8(path)));
            if (it != batch.end())
            {
                onReady(it->image);
            }
        },
        MediaThumbnailLoadingService::Priority::High);
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

            // Accumulated into m_advancedSearchSnapshotIcons -- see TabQuickSearchViewModel::
            // requestResultIcons/m_snapshotIcons for why.
            for (auto it = batch.constBegin(); it != batch.constEnd(); ++it)
            {
                m_advancedSearchSnapshotIcons.insert(it.key(), it.value());
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

void TabViewModel::requestMediaViewerResultThumbnails(int generation, const std::vector<FileNode>& candidates,
                                                        MediaThumbnailLoadingService::Priority priority)
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

            if (m_mediaViewerSortCriterion == MediaViewerSortCriterion::Duration)
            {
                // Re-orders the grid progressively as video durations finish decoding, instead of
                // only once (Architecture.md §14.68).
                reapplyMediaViewerSort();
            }

            if (m_mediaViewerProgressId != 0)
            {
                // De-duplicated by path (Architecture.md §14.66): a High-priority filter-match
                // dispatch and this kind's Low-priority background sweep can both decode the same
                // path, so raw batch-size summation would double-count it against
                // m_mediaViewerThumbnailsTotal. Only a path not already counted advances "done".
                for (auto it = batch.constBegin(); it != batch.constEnd(); ++it)
                {
                    if (m_mediaViewerCountedPaths.contains(it.key()))
                    {
                        continue;
                    }
                    m_mediaViewerCountedPaths.insert(it.key());
                    ++m_mediaViewerThumbnailsDone;
                }

                const std::size_t done = std::min(m_mediaViewerThumbnailsDone, m_mediaViewerThumbnailsTotal);
                m_notificationCenter.updateProgressBar(m_mediaViewerProgressId, 0, static_cast<int>(done),
                                                         static_cast<int>(m_mediaViewerThumbnailsTotal),
                                                         tr("Loading %1 item(s)...").arg(m_mediaViewerThumbnailsTotal),
                                                         tr("%1 of %2").arg(done).arg(m_mediaViewerThumbnailsTotal), QString());

                if (done >= m_mediaViewerThumbnailsTotal)
                {
                    m_notificationCenter.terminateProgressNotification(m_mediaViewerProgressId);
                    m_mediaViewerProgressId = 0;
                }
            }
        },
        priority);
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

void TabViewModel::requestMediaViewerResultTags(int generation, const std::vector<FileNode>& candidates)
{
    m_fileTagsService.requestTags(this, candidates, [this, generation](Result<TagsByPathMap> result) {
        if (generation != m_mediaViewerGeneration)
        {
            // Superseded/abandoned, same discard-by-generation-id approach
            // onMediaViewerCandidatesReady uses.
            return;
        }

        m_mediaViewerResultsModel->setFileTags(result ? result.value() : TagsByPathMap{});
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
