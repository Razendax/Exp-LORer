#pragma once

#include <optional>
#include <string>

#include "Language.h"

// Extension -> Language dispatch for the syntax-highlighting scope this pass supports
// (Architecture.md §14.26) -- same static-table-dispatch shape as MediaExtensions. Extensions
// FilePreviewUseCase already treats as plain text but that aren't one of the nine languages here
// (txt/xml/md/ini/log/yaml/csv/sql/sh/bat/ps1/xaml/toml) resolve to std::nullopt, leaving that
// preview unhighlighted exactly as before this feature existed.
namespace LanguageRegistry
{
    // `lowercaseExtension` must already be lowercased with a leading dot (e.g. ".cpp"), the same
    // shape MediaExtensions::lowercaseExtension produces.
    std::optional<Language> languageForExtension(const std::string& lowercaseExtension);
}
