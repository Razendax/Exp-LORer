#include "FileDecorationRulesStore.h"

#include <optional>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "JsonFileStore.h"

namespace
{
    constexpr int kCurrentVersion = 1;

    // Shared style-field extraction for a general rule object and for the two hidden-style
    // objects alike -- `patternsRaw` is passed in separately since the hidden objects have no
    // "patterns" key (Architecture.md §14.29). A malformed object (e.g. hand-edited JSON) falls
    // back to a no-override rule rather than failing the whole load, the store's usual tolerant
    // posture.
    Result<FileDecorationRule> ruleFromJson(const QJsonObject& ruleObject, std::string patternsRaw)
    {
        std::optional<std::string> hexColor;
        if (ruleObject.value(QStringLiteral("color")).isString())
        {
            hexColor = ruleObject.value(QStringLiteral("color")).toString().toStdString();
        }

        std::optional<std::string> fontFamily;
        if (ruleObject.value(QStringLiteral("fontFamily")).isString())
        {
            fontFamily = ruleObject.value(QStringLiteral("fontFamily")).toString().toStdString();
        }

        std::optional<int> fontPointSize;
        if (ruleObject.value(QStringLiteral("fontSize")).isDouble())
        {
            fontPointSize = ruleObject.value(QStringLiteral("fontSize")).toInt();
        }

        const bool bold = ruleObject.value(QStringLiteral("bold")).toBool();
        const bool italic = ruleObject.value(QStringLiteral("italic")).toBool();
        const bool underline = ruleObject.value(QStringLiteral("underline")).toBool();
        const bool strikeout = ruleObject.value(QStringLiteral("strikeout")).toBool();

        return FileDecorationRule::create(std::move(patternsRaw), hexColor, fontFamily, fontPointSize, bold, italic, underline, strikeout);
    }

    QJsonObject ruleToJson(const FileDecorationRule& rule, bool includePatterns)
    {
        QJsonObject ruleObject;
        if (includePatterns)
        {
            ruleObject[QStringLiteral("patterns")] = QString::fromStdString(rule.patternsRaw());
        }
        if (rule.hexColor())
        {
            ruleObject[QStringLiteral("color")] = QString::fromStdString(*rule.hexColor());
        }
        if (rule.fontFamily())
        {
            ruleObject[QStringLiteral("fontFamily")] = QString::fromStdString(*rule.fontFamily());
        }
        if (rule.fontPointSize())
        {
            ruleObject[QStringLiteral("fontSize")] = *rule.fontPointSize();
        }
        ruleObject[QStringLiteral("bold")] = rule.bold();
        ruleObject[QStringLiteral("italic")] = rule.italic();
        ruleObject[QStringLiteral("underline")] = rule.underline();
        ruleObject[QStringLiteral("strikeout")] = rule.strikeout();
        return ruleObject;
    }

    // Falls back to a no-override rule (empty patterns) when `key` is absent or not a JSON object
    // -- old-format files predating the hidden rows, or hand-edited/malformed JSON.
    FileDecorationRule hiddenRuleFromJson(const QJsonObject& root, const QString& key)
    {
        const QJsonValue value = root.value(key);
        if (value.isObject())
        {
            Result<FileDecorationRule> parsed = ruleFromJson(value.toObject(), "");
            if (parsed)
            {
                return std::move(parsed).value();
            }
        }
        return FileDecorationRule::create("", std::nullopt, std::nullopt, std::nullopt, false, false, false, false).value();
    }
}

FileDecorationRulesStore::FileDecorationRulesStore(std::filesystem::path rulesFilePath)
    : m_rulesFilePath(std::move(rulesFilePath))
{
}

FileDecorationRules FileDecorationRulesStore::load() const
{
    FileDecorationRules rules;

    const std::optional<QJsonObject> root = JsonFileStore::tryLoad(m_rulesFilePath, "FileDecorationRulesStore");
    if (!root)
    {
        return rules;
    }

    std::vector<FileDecorationRule> parsedRules;
    const QJsonArray ruleArray = root->value(QStringLiteral("rules")).toArray();
    for (const QJsonValue& value : ruleArray)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject ruleObject = value.toObject();
        std::string patternsRaw = ruleObject.value(QStringLiteral("patterns")).toString().toStdString();

        // A malformed persisted pattern (e.g. hand-edited JSON with an unterminated quote) is
        // dropped rather than failing the whole load -- tolerant load, same posture as
        // HighlightThemeStore skipping a Language it doesn't recognize.
        Result<FileDecorationRule> parsed = ruleFromJson(ruleObject, std::move(patternsRaw));
        if (parsed)
        {
            parsedRules.push_back(std::move(parsed).value());
        }
    }

    rules.setRules(std::move(parsedRules));
    rules.setHiddenFilesRule(hiddenRuleFromJson(*root, QStringLiteral("hiddenFiles")));
    rules.setHiddenFoldersRule(hiddenRuleFromJson(*root, QStringLiteral("hiddenFolders")));
    return rules;
}

bool FileDecorationRulesStore::save(const FileDecorationRules& rules) const
{
    QJsonArray ruleArray;
    for (const FileDecorationRule& rule : rules.rules())
    {
        ruleArray.append(ruleToJson(rule, /*includePatterns=*/true));
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("rules")] = ruleArray;
    root[QStringLiteral("hiddenFiles")] = ruleToJson(rules.hiddenFilesRule(), /*includePatterns=*/false);
    root[QStringLiteral("hiddenFolders")] = ruleToJson(rules.hiddenFoldersRule(), /*includePatterns=*/false);

    return JsonFileStore::trySave(m_rulesFilePath, root, "FileDecorationRulesStore");
}
