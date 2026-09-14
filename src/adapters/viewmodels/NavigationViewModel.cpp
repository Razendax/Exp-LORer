#include "NavigationViewModel.h"

#include "FileNavigationUseCase.h"

NavigationViewModel::NavigationViewModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
{
}

std::filesystem::path NavigationViewModel::currentPath() const
{
    return m_history.current().value_or(std::filesystem::path());
}

void NavigationViewModel::navigateTo(const std::filesystem::path& path)
{
    auto result = m_fileNavigationUseCase.listDirectory(path);
    if (result.hasError())
    {
        emit navigationFailed(path, QString::fromStdString(result.error().message));
        return;
    }

    m_history.navigate(path);
    setCurrentPath(path);
}

void NavigationViewModel::goUp()
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

void NavigationViewModel::goBack()
{
    const auto path = m_history.goBack();
    if (!path)
    {
        return;
    }

    setCurrentPath(*path);
}

void NavigationViewModel::goForward()
{
    const auto path = m_history.goForward();
    if (!path)
    {
        return;
    }

    setCurrentPath(*path);
}

void NavigationViewModel::setCurrentPath(std::filesystem::path path)
{
    emit currentPathChanged(path);
    emitAvailability();
}

void NavigationViewModel::emitAvailability()
{
    emit backAvailableChanged(m_history.canGoBack());
    emit forwardAvailableChanged(m_history.canGoForward());

    const auto current = m_history.current();
    const bool upAvailable = current.has_value() && current->parent_path() != *current;
    emit upAvailableChanged(upAvailable);
}
