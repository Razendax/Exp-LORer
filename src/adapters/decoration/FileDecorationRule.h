#pragma once

#include <optional>
#include <string>
#include <vector>

#include "FileNamePattern.h"
#include "Result.h"

// One user-defined "if a file/folder name matches, style the row like this" rule
// (Architecture.md §14.29), edited from Settings -> General -> Colors and persisted via
// FileDecorationRulesStore. Pure C++, no Qt -- colors are "#RRGGBB" hex strings and font families
// are plain names, converted to/from QColor/QFont only at the FileListModel/UI boundary, the same
// posture HighlightTheme already has for syntax-highlight colors.
class FileDecorationRule
{
public:
    // Parses `patternsRaw` (comma-separated, `"`-quoted/`""`-escaped, `#`-folder-marker syntax --
    // see Architecture.md §14.29) into the rule's pattern list. Fails only on an unterminated quote;
    // an empty or all-empty-token pattern list is accepted (it simply never matches anything).
    static Result<FileDecorationRule> create(std::string patternsRaw,
                                               std::optional<std::string> hexColor,
                                               std::optional<std::string> fontFamily,
                                               std::optional<int> fontPointSize,
                                               bool bold,
                                               bool italic,
                                               bool underline,
                                               bool strikeout);

    // True if any parsed pattern matches (OR semantics across the comma-separated list).
    bool matches(const std::string& nameUtf8, bool isDirectory) const;

    const std::string& patternsRaw() const noexcept { return m_patternsRaw; }
    const std::optional<std::string>& hexColor() const noexcept { return m_hexColor; }
    const std::optional<std::string>& fontFamily() const noexcept { return m_fontFamily; }
    std::optional<int> fontPointSize() const noexcept { return m_fontPointSize; }
    bool bold() const noexcept { return m_bold; }
    bool italic() const noexcept { return m_italic; }
    bool underline() const noexcept { return m_underline; }
    bool strikeout() const noexcept { return m_strikeout; }

    void setHexColor(std::optional<std::string> hexColor) { m_hexColor = std::move(hexColor); }
    void setFontFamily(std::optional<std::string> fontFamily) { m_fontFamily = std::move(fontFamily); }
    void setFontPointSize(std::optional<int> fontPointSize) noexcept { m_fontPointSize = fontPointSize; }
    void setBold(bool bold) noexcept { m_bold = bold; }
    void setItalic(bool italic) noexcept { m_italic = italic; }
    void setUnderline(bool underline) noexcept { m_underline = underline; }
    void setStrikeout(bool strikeout) noexcept { m_strikeout = strikeout; }

private:
    FileDecorationRule() = default;

    static Result<std::vector<FileNamePattern>> parsePatterns(const std::string& patternsRaw);

    std::string m_patternsRaw;
    std::vector<FileNamePattern> m_patterns;
    std::optional<std::string> m_hexColor;
    std::optional<std::string> m_fontFamily;
    std::optional<int> m_fontPointSize;
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
    bool m_strikeout = false;
};
