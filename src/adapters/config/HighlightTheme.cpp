#include "HighlightTheme.h"

#include <unordered_map>

std::optional<std::string> HighlightTheme::overrideFor(Language language, const std::string& captureName) const
{
    const auto languageIt = m_overrides.find(language);
    if (languageIt == m_overrides.end())
    {
        return std::nullopt;
    }

    const auto tokenIt = languageIt->second.find(captureName);
    if (tokenIt == languageIt->second.end())
    {
        return std::nullopt;
    }

    return tokenIt->second;
}

void HighlightTheme::setTokenColor(Language language, const std::string& captureName, const std::string& hexColor)
{
    m_overrides[language][captureName] = hexColor;
}

std::optional<std::string> HighlightTheme::colorFor(Language language, const std::string& captureName) const
{
    if (auto overridden = overrideFor(language, captureName))
    {
        return overridden;
    }
    return defaultColorFor(captureName);
}

std::optional<std::string> HighlightTheme::defaultColorFor(const std::string& captureName)
{
    static const std::unordered_map<std::string, std::string> kBasePalette{
        { "keyword", "#0000FF" },  { "string", "#008000" }, { "comment", "#808080" },
        { "number", "#008080" },   { "function", "#800080" }, { "type", "#0E7490" },
    };

    const std::string category = captureName.substr(0, captureName.find('.'));
    const auto it = kBasePalette.find(category);
    if (it == kBasePalette.end())
    {
        return std::nullopt;
    }
    return it->second;
}
