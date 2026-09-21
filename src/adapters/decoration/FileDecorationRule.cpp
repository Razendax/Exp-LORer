#include "FileDecorationRule.h"

#include <utility>

Result<std::vector<FileNamePattern>> FileDecorationRule::parsePatterns(const std::string& patternsRaw)
{
    std::vector<FileNamePattern> patterns;
    std::string token;
    bool inQuotes = false;

    size_t i = 0;
    while (i < patternsRaw.size())
    {
        const char c = patternsRaw[i];

        if (inQuotes)
        {
            if (c == '"')
            {
                // A doubled "" inside a quoted span is a literal " (CSV-style escaping); a single
                // " closes the quoted span.
                if (i + 1 < patternsRaw.size() && patternsRaw[i + 1] == '"')
                {
                    token += '"';
                    i += 2;
                    continue;
                }
                inQuotes = false;
                ++i;
                continue;
            }
            token += c;
            ++i;
            continue;
        }

        if (c == '"')
        {
            inQuotes = true;
            ++i;
            continue;
        }

        if (c == ',')
        {
            if (!token.empty())
            {
                patterns.emplace_back(std::move(token));
                token.clear();
            }
            ++i;
            continue;
        }

        token += c;
        ++i;
    }

    if (inQuotes)
    {
        return Result<std::vector<FileNamePattern>>::failure(
            Error(ErrorCode::InvalidArgument, "Unterminated \" in pattern list: " + patternsRaw));
    }

    if (!token.empty())
    {
        patterns.emplace_back(std::move(token));
    }

    return Result<std::vector<FileNamePattern>>::success(std::move(patterns));
}

Result<FileDecorationRule> FileDecorationRule::create(std::string patternsRaw,
                                                        std::optional<std::string> hexColor,
                                                        std::optional<std::string> fontFamily,
                                                        std::optional<int> fontPointSize,
                                                        bool bold,
                                                        bool italic,
                                                        bool underline,
                                                        bool strikeout)
{
    Result<std::vector<FileNamePattern>> parsedPatterns = parsePatterns(patternsRaw);
    if (!parsedPatterns)
    {
        return Result<FileDecorationRule>::failure(std::move(parsedPatterns).error());
    }

    FileDecorationRule rule;
    rule.m_patternsRaw = std::move(patternsRaw);
    rule.m_patterns = std::move(parsedPatterns).value();
    rule.m_hexColor = std::move(hexColor);
    rule.m_fontFamily = std::move(fontFamily);
    rule.m_fontPointSize = fontPointSize;
    rule.m_bold = bold;
    rule.m_italic = italic;
    rule.m_underline = underline;
    rule.m_strikeout = strikeout;

    return Result<FileDecorationRule>::success(std::move(rule));
}

bool FileDecorationRule::matches(const std::string& nameUtf8, bool isDirectory) const
{
    for (const auto& pattern : m_patterns)
    {
        if (pattern.matches(nameUtf8, isDirectory))
        {
            return true;
        }
    }
    return false;
}
