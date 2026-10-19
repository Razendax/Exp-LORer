#pragma once

#include <optional>
#include <string>

#include <QString>

#include "Language.h"

// Extension -> Language dispatch for the syntax-highlighting scope this pass supports
// (Architecture.md §14.26, expanded by §14.100) -- same static-table-dispatch shape as
// MediaExtensions. Extensions FilePreviewUseCase already treats as plain text but that aren't one
// of the twelve languages here (txt/xml/ini/log/csv/sql/sh/bat/xaml/toml/.markdown) resolve to
// std::nullopt, leaving that preview unhighlighted exactly as before this feature existed.
namespace LanguageRegistry
{
    // `lowercaseExtension` must already be lowercased with a leading dot (e.g. ".cpp"), the same
    // shape MediaExtensions::lowercaseExtension produces.
    std::optional<Language> languageForExtension(const std::string& lowercaseExtension);

    // Human-readable name for a Language value (e.g. Cpp -> "C++"), used wherever the enum is
    // presented in UI -- SettingsDialog's per-language theme list and TextEditorOptionsDialog's
    // highlighting combo box both call this instead of keeping their own copy of the mapping.
    QString displayName(Language language);
}
