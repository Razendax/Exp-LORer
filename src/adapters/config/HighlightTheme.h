#pragma once

#include <map>
#include <optional>
#include <string>

#include "Language.h"

// Per-token colors for text-preview syntax highlighting (Architecture.md §14.26), edited from
// SettingsDialog and persisted immediately via HighlightThemeStore -- a user *preference*, kept
// deliberately separate from AppConfig/AppConfigStore's session/window state (saved only on close,
// §14.14). Pure C++, no Qt: colors are stored as "#RRGGBB" hex strings, converted to/from QColor
// only at the UI boundary (SettingsDialog/SyntaxHighlighter).
//
// Only user overrides are stored here -- a captureName with no override falls back to
// defaultColorFor(), so a fresh install needs no seeding step and the persisted JSON stays small.
class HighlightTheme
{
public:
    // The color to use for `captureName` under `language`: the user's override if one was set via
    // setTokenColor, otherwise defaultColorFor(captureName). Returns std::nullopt only when neither
    // an override nor a built-in default exists (e.g. "variable"/"operator" captures, which are
    // left at the preview text view's own default foreground color by design).
    std::optional<std::string> colorFor(Language language, const std::string& captureName) const;

    void setTokenColor(Language language, const std::string& captureName, const std::string& hexColor);

    // std::nullopt when the user has never overridden this exact token for this language.
    std::optional<std::string> overrideFor(Language language, const std::string& captureName) const;

    // One shared base palette (Architecture.md §14.26): keyword/blue, string/green, comment/gray,
    // number/teal, function/purple, type/cyan -- derived from the capture name's first dot-segment
    // (e.g. "string.escape" -> "string") so it applies uniformly across every language's own
    // highlights.scm. Categories not in this list (variable, operator, punctuation.*, constant,
    // property, attribute, tag, ...) return std::nullopt -- "default-foreground", i.e. left
    // uncolored -- rather than guessing a color for tokens the plan didn't ask to be recolored.
    static std::optional<std::string> defaultColorFor(const std::string& captureName);

    const std::map<Language, std::map<std::string, std::string>>& overrides() const noexcept { return m_overrides; }
    void setOverrides(std::map<Language, std::map<std::string, std::string>> overrides) { m_overrides = std::move(overrides); }

private:
    std::map<Language, std::map<std::string, std::string>> m_overrides;
};
