#include "TabViewModel.h"

#include <algorithm>

#include "ArchiveExtensions.h"
#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "GitDirectoryStatus.h"
#include "GitStatusService.h"
#include "TagManagementUseCase.h"
#include "VirtualPaths.h"

namespace
{
    bool isCriteriaEmpty(const SearchCriteria& criteria)
    {
        return criteria.nameQuery.empty() && !criteria.minSizeBytes && !criteria.maxSizeBytes && criteria.extensionList.empty() &&
               criteria.tagIds.empty();
    }

    constexpr std::size_t kMonitoringLogCap = 200;
}

TabViewModel::TabViewModel(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                           const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                           GitStatusService& gitStatusService, FolderMonitorUseCase& folderMonitorUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_gitStatusService(gitStatusService)
    , m_folderMonitorUseCase(folderMonitorUseCase)
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
            [this](const std::filesystem::path& path, const std::vector<FileNode>&) {
                requestGitStatus(path);
                requestGitRepositorySummary(path);
            });
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
    loadAndApply(path, true);
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

    loadAndApply(*path, false);
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

    loadAndApply(*path, false);
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
    loadAndApply(currentPath(), false);
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

void TabViewModel::loadAndApply(const std::filesystem::path& path, bool recordHistory)
{
    auto result = m_fileNavigationUseCase.listDirectory(path);
    if (result.hasError())
    {
        emit navigationFailed(path, QString::fromStdString(result.error().message));
        return;
    }

    if (recordHistory)
    {
        m_history.navigate(path);
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
    auto result = m_fileNavigationUseCase.listDirectoryRecursive(m_searchRoot);
    if (result.hasError())
    {
        emit searchFailed(QString::fromStdString(result.error().message));
        return;
    }

    m_searchSnapshot = std::move(result).value();
    m_searchActive = true;
    m_searchQuery = query;

    auto filtered = FileNavigationUseCase::filterByName(m_searchSnapshot, query.toStdString());
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_searchResultsModel->setEntries(m_searchRoot, filtered);

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
    m_searchQuery.clear();
    m_searchRoot.clear();

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
    m_advancedSearchCriteria = SearchCriteria();
    m_advancedSearchRoot.clear();

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
    if (forceRescan || m_advancedSearchRoot.empty())
    {
        m_advancedSearchRoot = currentPath();
        auto result = m_fileNavigationUseCase.listDirectoryRecursive(m_advancedSearchRoot);
        if (result.hasError())
        {
            emit advancedSearchFailed(QString::fromStdString(result.error().message));
            return;
        }
        m_advancedSearchSnapshot = std::move(result).value();
    }

    auto filtered = FileNavigationUseCase::filterByCriteria(m_advancedSearchSnapshot, m_advancedSearchCriteria);

    if (!m_advancedSearchCriteria.tagIds.empty())
    {
        auto tagResult = m_tagManagementUseCase.findFilesWithAllTags(m_advancedSearchCriteria.tagIds);
        if (tagResult.hasError())
        {
            emit advancedSearchFailed(QString::fromStdString(tagResult.error().message));
            return;
        }

        std::vector<std::filesystem::path> allowedPaths;
        allowedPaths.reserve(tagResult.value().size());
        for (const FileTagAssociation& association : tagResult.value())
        {
            allowedPaths.push_back(association.filePath());
        }

        filtered = FileNavigationUseCase::filterByPaths(std::move(filtered), allowedPaths);
    }

    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_advancedSearchResultsModel->setEntries(m_advancedSearchRoot, filtered);

    if (!m_advancedSearchActive)
    {
        m_advancedSearchActive = true;
        setSelectedEntries({});
        emit advancedSearchModeChanged(true);
    }

    emit advancedSearchResultsChanged(filtered);
    emit advancedSearchCriteriaChanged(m_advancedSearchCriteria);
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

void TabViewModel::requestLocalBranches(std::function<void(Result<std::vector<std::string>>)> callback)
{
    if (!m_lastGitRepositorySummary.repositoryRoot)
    {
        callback(Result<std::vector<std::string>>::success({}));
        return;
    }

    m_gitStatusService.requestLocalBranches(this, *m_lastGitRepositorySummary.repositoryRoot, std::move(callback));
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
    if (m_monitoringActive)
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

    auto result = m_folderMonitorUseCase.startMonitoring(root, [this](const std::vector<FolderChangeEvent>& batch) {
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
    });

    if (!result)
    {
        emit monitoringFailed(QString::fromStdString(result.error().message));
        return;
    }

    m_monitoringId = result.value();
    m_monitoringActive = true;
    emit monitoringActiveChanged(true);
}

void TabViewModel::stopMonitoring()
{
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
