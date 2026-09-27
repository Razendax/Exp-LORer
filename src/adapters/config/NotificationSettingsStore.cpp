#include "NotificationSettingsStore.h"

#include <array>
#include <optional>
#include <utility>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "JsonFileStore.h"

namespace
{
    constexpr int kCurrentVersion = 1;

    QString toString(NotificationType type)
    {
        switch (type)
        {
            case NotificationType::Info:
                return QStringLiteral("Info");
            case NotificationType::Warning:
                return QStringLiteral("Warning");
            case NotificationType::Error:
                return QStringLiteral("Error");
        }
        return QString();
    }

    constexpr std::array<NotificationType, 3> kAllTypes = { NotificationType::Info, NotificationType::Warning,
                                                              NotificationType::Error };
}

NotificationSettingsStore::NotificationSettingsStore(std::filesystem::path settingsFilePath)
    : m_settingsFilePath(std::move(settingsFilePath))
{
}

NotificationSettings NotificationSettingsStore::load() const
{
    NotificationSettings settings;

    const std::optional<QJsonObject> root = JsonFileStore::tryLoad(m_settingsFilePath, "NotificationSettingsStore");
    if (!root)
    {
        return settings;
    }

    const QJsonObject types = root->value(QStringLiteral("types")).toObject();
    for (NotificationType type : kAllTypes)
    {
        const QJsonValue value = types.value(toString(type));
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject typeObject = value.toObject();
        const NotificationTypeSettings defaults = settings.settingsFor(type);
        const bool enabled = typeObject.value(QStringLiteral("enabled")).toBool(defaults.enabled);
        const int timeoutMs = typeObject.value(QStringLiteral("timeoutMs")).toInt(defaults.timeoutMs);
        settings.setSettingsFor(type, enabled, timeoutMs);
    }

    return settings;
}

bool NotificationSettingsStore::save(const NotificationSettings& settings) const
{
    QJsonObject types;
    for (NotificationType type : kAllTypes)
    {
        const NotificationTypeSettings typeSettings = settings.settingsFor(type);
        QJsonObject typeObject;
        typeObject[QStringLiteral("enabled")] = typeSettings.enabled;
        typeObject[QStringLiteral("timeoutMs")] = typeSettings.timeoutMs;
        types[toString(type)] = typeObject;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("types")] = types;

    return JsonFileStore::trySave(m_settingsFilePath, root, "NotificationSettingsStore");
}
