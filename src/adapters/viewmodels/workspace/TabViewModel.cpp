#include "TabViewModel.h"

#include <algorithm>
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
#include "PathUtf8.h"
#include "TagChangeNotifier.h"
#include "TagManagementUseCase.h"
#include "VirtualPaths.h"


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
    , m_fileListModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
    , m_quickSearch(new TabQuickSearchViewModel(advancedSearchService, fileTagsService, iconLoadingService, tagChangeNotifier,
                                                 fileDecorationRules, fileDecorationsChangeSource, this))
    , m_advancedSearch(new TabAdvancedSearchViewModel(fileNavigationUseCase, advancedSearchService, tagManagementUseCase,
                                                       fileTagsService, iconLoadingService, tagChangeNotifier, fileDecorationRules,
                                                       fileDecorationsChangeSource, this))
    , m_mediaViewer(new TabMediaViewerViewModel(advancedSearchService, tagManagementUseCase, fileTagsService, iconLoadingService,
                                                 mediaThumbnailLoadingService, tagChangeNotifier, notificationCenter,
                                                 fileDecorationRules, fileDecorationsChangeSource, this))
    , m_monitoring(new TabMonitoringViewModel(folderMonitorUseCase, this))
{
    connect(this, &TabViewModel::directoryContentsChanged, m_fileListModel, &FileListModel::setEntries);
    connect(m_fileListModel, &FileListModel::sortOrderChanged, this, &TabViewModel::sortOrderChanged);

    // Normal browsing only (Architecture.md §14.x's v1 scope cut) -- quick-search/advanced-search
    // results fire their own resultsChanged signals instead, which this deliberately does not
    // connect to.
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
    // signal meant to stay a plain broadcast. Quick Search/Advanced Search/Media Viewer each react
    // to the same event on their own, via their own TagChangeNotifier connections.
    connect(&m_tagChangeNotifier, &TagChangeNotifier::tagsChanged, this, [this]() { requestFileTags(currentPath(), m_fileListModel->entries()); });

    // Clearing the selection on a Quick/Advanced Search activation/deactivation edge mirrors what
    // applyFilter()/exit() did inline before extraction (Architecture.md §14.96) -- kept here,
    // rather than giving the sub-objects a back-reference, since selection is Selection-group
    // (TabViewModel's own) state.
    connect(m_quickSearch, &TabQuickSearchViewModel::modeChanged, this, [this](bool) { setSelectedEntries({}); });
    connect(m_advancedSearch, &TabAdvancedSearchViewModel::modeChanged, this, [this](bool) { setSelectedEntries({}); });
    connect(m_mediaViewer, &TabMediaViewerViewModel::modeChanged, this, [this](bool) { setSelectedEntries({}); });
}

TabViewModel::~TabViewModel()
{
    // Stopping an active monitor/terminating an active Media Viewer progress notification on
    // destruction is now TabMonitoringViewModel's/TabMediaViewerViewModel's own responsibility
    // (their destructors guard this the same way, Architecture.md §14.96) -- m_monitoring/
    // m_mediaViewer are child QObjects destroyed after this body runs.
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
    m_advancedSearch->exit();
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
    m_advancedSearch->exit();

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
    m_advancedSearch->exit();

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
    m_advancedSearch->exit();
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
    m_advancedSearch->setIconMask(newMask);
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
        m_advancedSearch->exit();
    }

    m_quickSearch->updateQuery(query, currentPath());
}

void TabViewModel::refreshSearch(const QString& query)
{
    if (!query.trimmed().isEmpty())
    {
        m_advancedSearch->exit();
    }

    m_quickSearch->refresh(query, currentPath());
}

void TabViewModel::showAdvancedSearchPanel()
{
    m_quickSearch->exit();
    m_advancedSearch->showPanel(currentPath());
}

void TabViewModel::startAdvancedSearch(const SearchCriteria& criteria)
{
    m_quickSearch->exit();
    m_advancedSearch->start(criteria, currentPath());
}

void TabViewModel::addTagSearchCriterion(Tag::Id tagId)
{
    m_quickSearch->exit();
    m_advancedSearch->addTagCriterion(tagId, currentPath());
}

void TabViewModel::enterMediaViewer()
{
    m_quickSearch->exit();
    m_advancedSearch->exit();
    m_mediaViewer->enter(currentPath());
}

void TabViewModel::applyMediaViewerTagToSelection(Tag::Id tagId)
{
    m_mediaViewer->applyTagToSelection(tagId, m_selectedEntries);
}

void TabViewModel::handleMediaViewerSearchTagActivated(Tag::Id tagId, bool ctrlHeld)
{
    m_mediaViewer->handleSearchTagActivated(tagId, ctrlHeld, m_selectedEntries);
}

void TabViewModel::acceptMediaViewerSearchTagQuery(const QString& query)
{
    m_mediaViewer->acceptSearchTagQuery(query, m_selectedEntries);
}

void TabViewModel::createAndApplyMediaViewerTag(const QString& name)
{
    m_mediaViewer->createAndApplyTag(name, m_selectedEntries);
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

Result<DriveSpaceInfo> TabViewModel::driveSpaceInfo() const
{
    return m_fileNavigationUseCase.driveSpaceInfo(currentPath());
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
