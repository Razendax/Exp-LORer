#include "ColumnVisibilitySettingsStore.h"

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "JsonFileStore.h"

namespace
{
    constexpr int kCurrentVersion = 1;

    // Every column except NameColumn (always visible, never exposed to the header menu or this
    // Settings page).
    constexpr std::array<FileListModel::Column, FileListModel::ColumnCount - 1> kAllColumns = {
        FileListModel::SizeColumn,      FileListModel::TypeColumn,   FileListModel::DateModifiedColumn,
        FileListModel::GitStatusColumn, FileListModel::TagsColumn,   FileListModel::DateCreatedColumn,
        FileListModel::PercentColumn,   FileListModel::FragmentsColumn,
    };

    QString toString(FileListModel::Column column)
    {
        switch (column)
        {
            case FileListModel::SizeColumn:
                return QStringLiteral("Size");
            case FileListModel::TypeColumn:
                return QStringLiteral("Type");
            case FileListModel::DateModifiedColumn:
                return QStringLiteral("DateModified");
            case FileListModel::GitStatusColumn:
                return QStringLiteral("GitStatus");
            case FileListModel::TagsColumn:
                return QStringLiteral("Tags");
            case FileListModel::DateCreatedColumn:
                return QStringLiteral("DateCreated");
            case FileListModel::PercentColumn:
                return QStringLiteral("Percent");
            case FileListModel::FragmentsColumn:
                return QStringLiteral("Fragments");
            default:
                return QString();
        }
    }
}

ColumnVisibilitySettingsStore::ColumnVisibilitySettingsStore(std::filesystem::path settingsFilePath)
    : m_settingsFilePath(std::move(settingsFilePath))
{
}

ColumnVisibilitySettings ColumnVisibilitySettingsStore::load() const
{
    ColumnVisibilitySettings settings;

    const std::optional<QJsonObject> root = JsonFileStore::tryLoad(m_settingsFilePath, "ColumnVisibilitySettingsStore");
    if (!root)
    {
        return settings;
    }

    const QJsonObject columns = root->value(QStringLiteral("columns")).toObject();
    for (FileListModel::Column column : kAllColumns)
    {
        const QJsonValue value = columns.value(toString(column));
        if (value.isBool())
        {
            settings.visible[static_cast<std::size_t>(column)] = value.toBool();
        }
    }

    return settings;
}

bool ColumnVisibilitySettingsStore::save(const ColumnVisibilitySettings& settings) const
{
    QJsonObject columns;
    for (FileListModel::Column column : kAllColumns)
    {
        columns[toString(column)] = settings.visible[static_cast<std::size_t>(column)];
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("columns")] = columns;

    return JsonFileStore::trySave(m_settingsFilePath, root, "ColumnVisibilitySettingsStore");
}
