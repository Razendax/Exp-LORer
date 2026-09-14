#pragma once

#include <filesystem>
#include <optional>
#include <vector>

// A single recorded navigation step (Command pattern per Architecture.md §14.3).
class NavigateToPathCommand
{
public:
    explicit NavigateToPathCommand(std::filesystem::path path)
        : m_path(std::move(path))
    {
    }

    const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

// Per-tab back/forward navigation stack. Bookkeeping only: it never touches disk, and does not
// re-validate paths itself (Architecture.md §14.6) — callers (e.g. TabViewModel) are
// expected to validate a path before recording it via navigate().
class NavigationHistory
{
public:
    // Records a new navigation: the previous current path becomes the top of the back stack, and
    // any forward history is discarded.
    void navigate(std::filesystem::path path);

    // Moves the current pointer one step back/forward and returns the resulting path, or
    // std::nullopt if there is nothing to go back/forward to.
    std::optional<std::filesystem::path> goBack();
    std::optional<std::filesystem::path> goForward();

    bool canGoBack() const noexcept;
    bool canGoForward() const noexcept;

    std::optional<std::filesystem::path> current() const;

private:
    std::vector<NavigateToPathCommand> m_backStack;
    std::vector<NavigateToPathCommand> m_forwardStack;
    std::optional<NavigateToPathCommand> m_current;
};
