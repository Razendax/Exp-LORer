#include "NavigationHistory.h"

void NavigationHistory::navigate(std::filesystem::path path)
{
    if (m_current)
    {
        m_backStack.push_back(*m_current);
    }

    m_current = NavigateToPathCommand(std::move(path));
    m_forwardStack.clear();
}

std::optional<std::filesystem::path> NavigationHistory::goBack()
{
    if (m_backStack.empty())
    {
        return std::nullopt;
    }

    if (m_current)
    {
        m_forwardStack.push_back(*m_current);
    }

    m_current = m_backStack.back();
    m_backStack.pop_back();

    return m_current->path();
}

std::optional<std::filesystem::path> NavigationHistory::goForward()
{
    if (m_forwardStack.empty())
    {
        return std::nullopt;
    }

    if (m_current)
    {
        m_backStack.push_back(*m_current);
    }

    m_current = m_forwardStack.back();
    m_forwardStack.pop_back();

    return m_current->path();
}

bool NavigationHistory::canGoBack() const noexcept
{
    return !m_backStack.empty();
}

bool NavigationHistory::canGoForward() const noexcept
{
    return !m_forwardStack.empty();
}

std::optional<std::filesystem::path> NavigationHistory::current() const
{
    if (!m_current)
    {
        return std::nullopt;
    }

    return m_current->path();
}
