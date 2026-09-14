#include "TabViewModel.h"

#include "FileListModel.h"
#include "FileNavigationUseCase.h"

TabViewModel::TabViewModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_fileListModel(new FileListModel(this))
{
    connect(this, &TabViewModel::directoryContentsChanged, m_fileListModel, &FileListModel::setEntries);
}

std::filesystem::path TabViewModel::currentPath() const
{
    return m_history.current().value_or(std::filesystem::path());
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

    const auto parent = current->parent_path();
    if (parent == *current)
    {
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

void TabViewModel::setViewMode(ViewMode mode)
{
    if (mode == m_viewMode)
    {
        return;
    }

    m_viewMode = mode;
    emit viewModeChanged(mode);
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
    const bool upAvailable = current.has_value() && current->parent_path() != *current;
    emit upAvailableChanged(upAvailable);
}
