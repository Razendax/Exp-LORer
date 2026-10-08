#include "WorkspaceStore.h"

#include <algorithm>
#include <optional>
#include <system_error>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUuid>

#include "AppConfigStore.h"
#include "JsonFileStore.h"

namespace
{
    constexpr int kCurrentVersion = 1;

    constexpr const char* kDefaultWorkspaceId = "default";

    QString defaultWorkspaceName()
    {
        return QObject::tr("Default");
    }

    QJsonObject entryToJson(const WorkspaceEntry& entry)
    {
        QJsonObject json;
        json[QStringLiteral("id")] = entry.id;
        json[QStringLiteral("name")] = entry.name;
        return json;
    }

    // std::nullopt-like skip: an entry missing/empty "id" is dropped rather than failing the whole
    // load, same tolerant-load posture as FavoritesStore's itemFromJson.
    bool entryFromJson(const QJsonValue& json, WorkspaceEntry& outEntry)
    {
        if (!json.isObject())
        {
            return false;
        }

        const QJsonObject object = json.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
        {
            return false;
        }

        outEntry.id = id;
        outEntry.name = object.value(QStringLiteral("name")).toString();
        return true;
    }
}

WorkspaceStore::WorkspaceStore(std::filesystem::path appDataDir)
    : m_appDataDir(std::move(appDataDir))
    , m_workspacesDir(m_appDataDir / "workspaces")
    , m_indexFilePath(m_workspacesDir / "index.json")
    , m_legacyConfigFilePath(m_appDataDir / "config.json")
{
}

std::vector<WorkspaceEntry> WorkspaceStore::loadIndex() const
{
    std::vector<WorkspaceEntry> entries;

    const std::optional<QJsonObject> root = JsonFileStore::tryLoad(m_indexFilePath, "WorkspaceStore");
    if (!root)
    {
        return entries;
    }

    for (const QJsonValue& value : root->value(QStringLiteral("workspaces")).toArray())
    {
        WorkspaceEntry entry;
        if (entryFromJson(value, entry))
        {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

bool WorkspaceStore::saveIndex(const std::vector<WorkspaceEntry>& entries) const
{
    QJsonArray array;
    for (const WorkspaceEntry& entry : entries)
    {
        array.append(entryToJson(entry));
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("workspaces")] = array;

    return JsonFileStore::trySave(m_indexFilePath, root, "WorkspaceStore");
}

std::vector<WorkspaceEntry> WorkspaceStore::ensureMigratedAndNonEmpty() const
{
    if (std::filesystem::exists(m_indexFilePath))
    {
        std::vector<WorkspaceEntry> entries = loadIndex();
        if (!entries.empty())
        {
            return entries;
        }

        // Empty/corrupt index -- self-heal to a single default entry (same tolerant-fallback
        // posture AppConfigStore::load() has for a missing file).
        entries.push_back(WorkspaceEntry{ QString::fromLatin1(kDefaultWorkspaceId), defaultWorkspaceName() });
        saveIndex(entries);
        return entries;
    }

    // First run: one-time migration of a pre-existing legacy config.json into
    // workspaces/default.json, or just seed an empty default workspace if there was none.
    const WorkspaceEntry defaultEntry{ QString::fromLatin1(kDefaultWorkspaceId), defaultWorkspaceName() };

    if (std::filesystem::exists(m_legacyConfigFilePath))
    {
        const AppConfigStore legacyStore(m_legacyConfigFilePath);
        const AppConfigStore defaultStore(configFilePath(defaultEntry.id));
        defaultStore.save(legacyStore.load());
    }

    const std::vector<WorkspaceEntry> entries{ defaultEntry };
    saveIndex(entries);
    return entries;
}

std::vector<WorkspaceEntry> WorkspaceStore::listWorkspaces() const
{
    return ensureMigratedAndNonEmpty();
}

std::filesystem::path WorkspaceStore::configFilePath(const QString& id) const
{
    return m_workspacesDir / (id.toStdWString() + L".json");
}

QString WorkspaceStore::createWorkspace(const QString& name)
{
    std::vector<WorkspaceEntry> entries = ensureMigratedAndNonEmpty();

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entries.push_back(WorkspaceEntry{ id, name });
    saveIndex(entries);

    return id;
}

bool WorkspaceStore::renameWorkspace(const QString& id, const QString& newName)
{
    std::vector<WorkspaceEntry> entries = ensureMigratedAndNonEmpty();

    bool found = false;
    for (WorkspaceEntry& entry : entries)
    {
        if (entry.id == id)
        {
            entry.name = newName;
            found = true;
            break;
        }
    }

    return found && saveIndex(entries);
}

bool WorkspaceStore::deleteWorkspace(const QString& id)
{
    std::vector<WorkspaceEntry> entries = ensureMigratedAndNonEmpty();

    const auto it = std::find_if(entries.begin(), entries.end(), [&id](const WorkspaceEntry& entry) { return entry.id == id; });
    if (it == entries.end())
    {
        return false;
    }

    entries.erase(it);
    if (!saveIndex(entries))
    {
        return false;
    }

    std::error_code errorCode;
    std::filesystem::remove(configFilePath(id), errorCode);
    return true;
}
