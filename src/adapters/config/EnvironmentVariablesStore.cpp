#include "EnvironmentVariablesStore.h"

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

EnvironmentVariablesStore::EnvironmentVariablesStore(std::filesystem::path environmentVariablesFilePath)
    : m_environmentVariablesFilePath(std::move(environmentVariablesFilePath))
{
}

std::vector<EnvironmentVariable> EnvironmentVariablesStore::load() const
{
    std::vector<EnvironmentVariable> variables;

    const std::optional<QJsonObject> root =
        JsonFileStore::tryLoad(m_environmentVariablesFilePath, "EnvironmentVariablesStore");
    if (!root)
    {
        return variables;
    }

    for (const QJsonValue& value : root->value(QStringLiteral("variables")).toArray())
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            continue;
        }

        EnvironmentVariable variable;
        variable.name = name;
        for (const QJsonValue& valueEntry : object.value(QStringLiteral("values")).toArray())
        {
            variable.values.append(valueEntry.toString());
        }
        variables.push_back(std::move(variable));
    }

    return variables;
}

bool EnvironmentVariablesStore::save(const std::vector<EnvironmentVariable>& variables) const
{
    QJsonArray variableArray;
    for (const EnvironmentVariable& variable : variables)
    {
        QJsonArray valueArray;
        for (const QString& value : variable.values)
        {
            valueArray.append(value);
        }

        QJsonObject json;
        json[QStringLiteral("name")] = variable.name;
        json[QStringLiteral("values")] = valueArray;
        variableArray.append(json);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("variables")] = variableArray;

    return JsonFileStore::trySave(m_environmentVariablesFilePath, root, "EnvironmentVariablesStore");
}
