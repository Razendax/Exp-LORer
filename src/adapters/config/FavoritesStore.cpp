#include "FavoritesStore.h"

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

    // Same round-tripping idiom as AppConfigStore.cpp -- QString::fromStdWString/toStdWString --
    // rather than PathUtf8::toUtf8(), which is reserved for the SQLite boundary/narrow comparisons
    // (Architecture.md §6). QString round-trips a wstring losslessly on Windows.
    QString pathToQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    std::filesystem::path qStringToPath(const QString& text)
    {
        return std::filesystem::path(text.toStdWString());
    }

    QJsonObject itemToJson(const FavoriteItem& item)
    {
        QJsonObject json;
        json[QStringLiteral("id")] = static_cast<double>(item.id());
        json[QStringLiteral("path")] = pathToQString(item.path());
        return json;
    }

    // std::nullopt if `json` isn't an object, or its "path" is missing/empty -- a malformed
    // persisted item is skipped rather than failing the whole load (tolerant-load posture, same
    // as FileDecorationRulesStore dropping an unparsable rule).
    std::optional<FavoriteItem> itemFromJson(const QJsonValue& json)
    {
        if (!json.isObject())
        {
            return std::nullopt;
        }

        const QJsonObject object = json.toObject();
        const QString pathText = object.value(QStringLiteral("path")).toString();
        if (pathText.isEmpty())
        {
            return std::nullopt;
        }

        const auto id = static_cast<FavoriteItem::Id>(object.value(QStringLiteral("id")).toDouble());
        return FavoriteItem(id, qStringToPath(pathText));
    }

    QJsonObject categoryToJson(const FavoriteCategory& category)
    {
        QJsonArray items;
        for (const FavoriteItem& item : category.items())
        {
            items.append(itemToJson(item));
        }

        QJsonObject json;
        json[QStringLiteral("kind")] = QStringLiteral("category");
        json[QStringLiteral("id")] = static_cast<double>(category.id());
        json[QStringLiteral("name")] = QString::fromStdString(category.name());
        json[QStringLiteral("items")] = items;
        return json;
    }

    // A malformed item inside a well-formed category object is just skipped -- same tolerant-load
    // posture, one level down.
    FavoriteCategory categoryFromJson(const QJsonObject& object)
    {
        const auto id = static_cast<FavoriteCategory::Id>(object.value(QStringLiteral("id")).toDouble());
        FavoriteCategory category(id, object.value(QStringLiteral("name")).toString().toStdString());

        for (const QJsonValue& itemValue : object.value(QStringLiteral("items")).toArray())
        {
            if (auto item = itemFromJson(itemValue))
            {
                category.items().push_back(std::move(*item));
            }
        }
        return category;
    }
}

FavoritesStore::FavoritesStore(std::filesystem::path favoritesFilePath)
    : m_favoritesFilePath(std::move(favoritesFilePath))
{
}

Favorites FavoritesStore::load() const
{
    Favorites favorites;

    const std::optional<QJsonObject> root = JsonFileStore::tryLoad(m_favoritesFilePath, "FavoritesStore");
    if (!root)
    {
        return favorites;
    }

    std::vector<Favorites::Entry> entries;
    for (const QJsonValue& value : root->value(QStringLiteral("entries")).toArray())
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject object = value.toObject();
        const QString kind = object.value(QStringLiteral("kind")).toString();

        if (kind == QStringLiteral("item"))
        {
            if (auto item = itemFromJson(value))
            {
                entries.emplace_back(std::move(*item));
            }
        }
        else if (kind == QStringLiteral("category"))
        {
            entries.emplace_back(categoryFromJson(object));
        }
        // Any other/missing "kind" is a malformed entry, skipped.
    }

    favorites.setEntries(std::move(entries));
    favorites.setNextId(static_cast<Favorites::Id>(root->value(QStringLiteral("nextId")).toDouble(1)));
    return favorites;
}

bool FavoritesStore::save(const Favorites& favorites) const
{
    QJsonArray entryArray;
    for (const Favorites::Entry& entry : favorites.entries())
    {
        if (const auto* item = std::get_if<FavoriteItem>(&entry))
        {
            QJsonObject json = itemToJson(*item);
            json[QStringLiteral("kind")] = QStringLiteral("item");
            entryArray.append(json);
        }
        else if (const auto* category = std::get_if<FavoriteCategory>(&entry))
        {
            entryArray.append(categoryToJson(*category));
        }
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("nextId")] = static_cast<double>(favorites.nextId());
    root[QStringLiteral("entries")] = entryArray;

    return JsonFileStore::trySave(m_favoritesFilePath, root, "FavoritesStore");
}
