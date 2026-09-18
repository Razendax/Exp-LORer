#include "TabViewModel.h"

#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "VirtualPaths.h"

namespace
{
    bool isCriteriaEmpty(const SearchCriteria& criteria)
    {
        return criteria.nameQuery.empty() && !criteria.minSizeBytes && !criteria.maxSizeBytes && criteria.extensionList.empty();
    }
}

TabViewModel::TabViewModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_fileListModel(new FileListModel(this))
    , m_searchResultsModel(new FileListModel(this))
    , m_advancedSearchResultsModel(new FileListModel(this))
{
    connect(this, &TabViewModel::directoryContentsChanged, m_fileListModel, &FileListModel::setEntries);
    connect(m_fileListModel, &FileListModel::sortOrderChanged, this, &TabViewModel::sortOrderChanged);
}

std::filesystem::path TabViewModel::currentPath() const
{
    return m_history.current().value_or(std::filesystem::path());
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

void TabViewModel::goUp()
{
    const auto current = m_history.current();
    if (!current)
    {
        return;
    }

    if (*current == VirtualPaths::ThisPC)
    {
        return;
    }

    const auto parent = current->parent_path();
    if (parent == *current)
    {
        // A filesystem root (local drive root or UNC share root) — This PC is its "exit" point.
        navigateTo(VirtualPaths::ThisPC);
        return;
    }

    navigateTo(parent);
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

    emit searchModeChanged(false);
}

void TabViewModel::showAdvancedSearchPanel()
{
    if (m_advancedSearchActive)
    {
        return;
    }

    exitSearch();

    m_advancedSearchActive = true;
    emit advancedSearchModeChanged(true);
}

void TabViewModel::startAdvancedSearch(const SearchCriteria& criteria)
{
    if (isCriteriaEmpty(criteria))
    {
        return;
    }

    exitSearch();

    m_advancedSearchRoot = currentPath();
    auto result = m_fileNavigationUseCase.listDirectoryRecursive(m_advancedSearchRoot);
    if (result.hasError())
    {
        emit advancedSearchFailed(QString::fromStdString(result.error().message));
        return;
    }

    m_advancedSearchSnapshot = std::move(result).value();
    m_advancedSearchActive = true;
    m_advancedSearchCriteria = criteria;

    auto filtered = FileNavigationUseCase::filterByCriteria(m_advancedSearchSnapshot, criteria);
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_advancedSearchResultsModel->setEntries(m_advancedSearchRoot, filtered);

    emit advancedSearchModeChanged(true);
    emit advancedSearchResultsChanged(filtered);
}

void TabViewModel::updateAdvancedSearchCriteria(const SearchCriteria& criteria)
{
    if (!m_advancedSearchActive)
    {
        return;
    }

    m_advancedSearchCriteria = criteria;

    auto filtered = FileNavigationUseCase::filterByCriteria(m_advancedSearchSnapshot, criteria);
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_advancedSearchResultsModel->setEntries(m_advancedSearchRoot, filtered);

    emit advancedSearchResultsChanged(filtered);
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

    emit advancedSearchModeChanged(false);
}

void TabViewModel::emitAvailability()
{
    emit backAvailableChanged(m_history.canGoBack());
    emit forwardAvailableChanged(m_history.canGoForward());

    const auto current = m_history.current();
    const bool upAvailable = current.has_value() && *current != VirtualPaths::ThisPC;
    emit upAvailableChanged(upAvailable);
}
