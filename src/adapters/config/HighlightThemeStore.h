#pragma once

#include <filesystem>

#include "HighlightTheme.h"

// Loads/saves HighlightTheme as JSON via Qt's built-in QJsonDocument (Architecture.md §14.26) --
// same tolerant-load/atomic-save shape as AppConfigStore, in its own highlight_theme.json file
// (deliberately separate from config.json: this is a persistent user preference edited from
// Settings and saved immediately, not session/window state saved only on close).
class HighlightThemeStore
{
public:
    explicit HighlightThemeStore(std::filesystem::path themeFilePath);

    HighlightTheme load() const;
    bool save(const HighlightTheme& theme) const;

private:
    std::filesystem::path m_themeFilePath;
};
