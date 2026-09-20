#include "HighlightThemeStore.h"

#include <optional>

#include <QDebug>
#include <QJsonObject>
#include <QString>

#include "JsonFileStore.h"

namespace
{
    constexpr int kCurrentVersion = 1;

    QString toString(Language language)
    {
        switch (language)
        {
            case Language::C:
                return QStringLiteral("C");
            case Language::Cpp:
                return QStringLiteral("Cpp");
            case Language::CSharp:
                return QStringLiteral("CSharp");
            case Language::Python:
                return QStringLiteral("Python");
            case Language::JavaScript:
                return QStringLiteral("JavaScript");
            case Language::TypeScript:
                return QStringLiteral("TypeScript");
            case Language::Json:
                return QStringLiteral("Json");
            case Language::Html:
                return QStringLiteral("Html");
            case Language::Css:
                return QStringLiteral("Css");
        }
        return QString();
    }

    std::optional<Language> languageFromString(const QString& text)
    {
        if (text == QStringLiteral("C"))
            return Language::C;
        if (text == QStringLiteral("Cpp"))
            return Language::Cpp;
        if (text == QStringLiteral("CSharp"))
            return Language::CSharp;
        if (text == QStringLiteral("Python"))
            return Language::Python;
        if (text == QStringLiteral("JavaScript"))
            return Language::JavaScript;
        if (text == QStringLiteral("TypeScript"))
            return Language::TypeScript;
        if (text == QStringLiteral("Json"))
            return Language::Json;
        if (text == QStringLiteral("Html"))
            return Language::Html;
        if (text == QStringLiteral("Css"))
            return Language::Css;
        return std::nullopt;
    }
}

HighlightThemeStore::HighlightThemeStore(std::filesystem::path themeFilePath)
    : m_themeFilePath(std::move(themeFilePath))
{
}

HighlightTheme HighlightThemeStore::load() const
{
    HighlightTheme theme;

    const std::optional<QJsonObject> root = JsonFileStore::tryLoad(m_themeFilePath, "HighlightThemeStore");
    if (!root)
    {
        return theme;
    }

    std::map<Language, std::map<std::string, std::string>> overrides;

    const QJsonObject languages = root->value(QStringLiteral("languages")).toObject();
    for (auto languageIt = languages.constBegin(); languageIt != languages.constEnd(); ++languageIt)
    {
        const std::optional<Language> language = languageFromString(languageIt.key());
        if (!language || !languageIt.value().isObject())
        {
            continue;
        }

        std::map<std::string, std::string> tokenColors;
        const QJsonObject tokens = languageIt.value().toObject();
        for (auto tokenIt = tokens.constBegin(); tokenIt != tokens.constEnd(); ++tokenIt)
        {
            if (tokenIt.value().isString())
            {
                tokenColors[tokenIt.key().toStdString()] = tokenIt.value().toString().toStdString();
            }
        }

        if (!tokenColors.empty())
        {
            overrides[*language] = std::move(tokenColors);
        }
    }

    theme.setOverrides(std::move(overrides));
    return theme;
}

bool HighlightThemeStore::save(const HighlightTheme& theme) const
{
    QJsonObject languages;
    for (const auto& [language, tokenColors] : theme.overrides())
    {
        const QString key = toString(language);
        if (key.isEmpty())
        {
            // toString() only falls through to "" for a Language value it doesn't map (i.e. the
            // enum was extended without updating this switch) -- writing that under key "" would
            // silently collide with/overwrite another unmapped language's colors on this and every
            // future save, so skip it and say why instead.
            qWarning() << "HighlightThemeStore: no JSON key for Language value" << static_cast<int>(language)
                       << "-- its overrides will not be saved";
            continue;
        }

        QJsonObject tokens;
        for (const auto& [captureName, hexColor] : tokenColors)
        {
            tokens[QString::fromStdString(captureName)] = QString::fromStdString(hexColor);
        }
        languages[key] = tokens;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("languages")] = languages;

    return JsonFileStore::trySave(m_themeFilePath, root, "HighlightThemeStore");
}
