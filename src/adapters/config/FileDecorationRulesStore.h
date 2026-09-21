#pragma once

#include <filesystem>

#include "FileDecorationRules.h"

// Loads/saves FileDecorationRules as JSON via Qt's QJsonDocument (Architecture.md §14.29) -- same
// tolerant-load/atomic-save shape as HighlightThemeStore, in its own file_decorations.json
// (a persistent user preference edited from Settings and saved immediately, deliberately separate
// from config.json's session/window state).
class FileDecorationRulesStore
{
public:
    explicit FileDecorationRulesStore(std::filesystem::path rulesFilePath);

    FileDecorationRules load() const;
    bool save(const FileDecorationRules& rules) const;

private:
    std::filesystem::path m_rulesFilePath;
};
