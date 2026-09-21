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

        const std::string patternsRaw = ruleObject.value(QStringLiteral("patterns")).toString().toStdString();

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

        // A malformed persisted pattern (e.g. hand-edited JSON with an unterminated quote) is
        // dropped rather than failing the whole load -- tolerant load, same posture as
        // HighlightThemeStore skipping a Language it doesn't recognize.
        Result<FileDecorationRule> parsed =
            FileDecorationRule::create(patternsRaw, hexColor, fontFamily, fontPointSize, bold, italic, underline, strikeout);
        if (parsed)
        {
            parsedRules.push_back(std::move(parsed).value());
        }
    }

    rules.setRules(std::move(parsedRules));
    return rules;
}

bool FileDecorationRulesStore::save(const FileDecorationRules& rules) const
{
    QJsonArray ruleArray;
    for (const FileDecorationRule& rule : rules.rules())
    {
        QJsonObject ruleObject;
        ruleObject[QStringLiteral("patterns")] = QString::fromStdString(rule.patternsRaw());
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
        ruleArray.append(ruleObject);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("rules")] = ruleArray;

    return JsonFileStore::trySave(m_rulesFilePath, root, "FileDecorationRulesStore");
}
