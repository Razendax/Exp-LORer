#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <QString>

#include "Language.h"

// Extension -> Language dispatch for the syntax-highlighting scope this pass supports
// (Architecture.md §14.26, expanded by §14.100/§14.107) -- same static-table-dispatch shape as
// MediaExtensions. Extensions FilePreviewUseCase already treats as plain text but that aren't one
// of the thirteen languages here (txt/xml/ini/log/csv/sql/sh/bat/xaml/toml/.markdown) resolve to
// std::nullopt, leaving that preview unhighlighted exactly as before this feature existed.
namespace LanguageRegistry
{
    // `lowercaseExtension` must already be lowercased with a leading dot (e.g. ".cpp"), the same
    // shape MediaExtensions::lowercaseExtension produces.
    std::optional<Language> languageForExtension(const std::string& lowercaseExtension);

    // Same dispatch as languageForExtension, but from the full path rather than just its
    // extension -- needed for CMake (§14.107): `CMakeLists.txt` is the language's defining file
    // but carries no CMake-specific extension of its own (it's a plain `.txt` file by name), so
    // the filename itself has to be checked first. Falls back to
    // languageForExtension(lowercased path.extension()) for everything else.
    std::optional<Language> languageForPath(const std::filesystem::path& path);

    // Human-readable name for a Language value (e.g. Cpp -> "C++"), used wherever the enum is
    // presented in UI -- SettingsDialog's per-language theme list and TextEditorOptionsDialog's
    // highlighting combo box both call this instead of keeping their own copy of the mapping.
    QString displayName(Language language);
}
