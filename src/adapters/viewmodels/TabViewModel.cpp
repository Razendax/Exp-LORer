#include "TabViewModel.h"

#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "VirtualPaths.h"

TabViewModel::TabViewModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_fileListModel(new FileListModel(this))
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

void TabViewModel::navigateTo(const std::filesystem::path& path)
{
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
    const auto path = m_history.goBack();
    if (!path)
    {
        return;
    }

    loadAndApply(*path, false);
}

void TabViewModel::goForward()
{
    const auto path = m_history.goForward();
    if (!path)
    {
        return;
    }

    loadAndApply(*path, false);
}

void TabViewModel::setSelectedEntry(const std::optional<FileNode>& entry)
{
    if (m_selectedEntry == entry)
    {
        return;
    }

    m_selectedEntry = entry;
    emit selectedEntryChanged(entry);
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

    setSelectedEntry(std::nullopt);

    emit currentPathChanged(path);
    emit directoryContentsChanged(path, result.value());
    emitAvailability();
}

void TabViewModel::emitAvailability()
{
    emit backAvailableChanged(m_history.canGoBack());
    emit forwardAvailableChanged(m_history.canGoForward());

    const auto current = m_history.current();
    const bool upAvailable = current.has_value() && *current != VirtualPaths::ThisPC;
    emit upAvailableChanged(upAvailable);
}
