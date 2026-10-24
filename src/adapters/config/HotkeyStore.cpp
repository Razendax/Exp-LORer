#include "HotkeyStore.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "HotkeyCatalog.h"
#include "JsonFileStore.h"

namespace
{
    constexpr int kCurrentVersion = 1;

    // Every stableId the current build's catalog knows about -- an id outside this set is either a
    // downgrade (a newer version's action) or a since-removed action, and is skipped on load
    // (tolerant-load posture, same as FavoritesStore dropping a malformed entry).
    const std::unordered_set<std::string>& knownStableIds()
    {
        static const std::unordered_set<std::string> ids = [] {
            std::unordered_set<std::string> result;
            for (const HotkeyCatalogEntry& entry : HotkeyCatalog::entries())
            {
                result.insert(entry.stableId);
            }
            return result;
        }();
        return ids;
    }
}

HotkeyStore::HotkeyStore(std::filesystem::path hotkeysFilePath)
    : m_hotkeysFilePath(std::move(hotkeysFilePath))
{
}

HotkeyBindings HotkeyStore::load() const
{
    HotkeyBindings bindings;

    const std::optional<QJsonObject> root = JsonFileStore::tryLoad(m_hotkeysFilePath, "HotkeyStore");
    if (!root)
    {
        return bindings;
    }

    std::unordered_map<std::string, HotkeyBindings::Override> overrides;
    std::uint64_t maxOrder = 0;

    for (const QJsonValue& value : root->value(QStringLiteral("overrides")).toArray())
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject object = value.toObject();
        const QString idText = object.value(QStringLiteral("id")).toString();
        if (idText.isEmpty())
        {
            continue; // Malformed entry, skipped.
        }

        const std::string id = idText.toStdString();
        if (knownStableIds().find(id) == knownStableIds().end())
        {
            continue; // Unknown id (downgrade or removed action), skipped.
        }

        // An empty "chord" string round-trips as an explicit Clear override (HotkeyChord::
        // fromStorageString returns an empty HotkeyChord for an empty string) -- not skipped like
        // a malformed entry above.
        const QString chordText = object.value(QStringLiteral("chord")).toString();
        const auto order = static_cast<std::uint64_t>(object.value(QStringLiteral("order")).toDouble());

        overrides[id] = HotkeyBindings::Override{ HotkeyChord::fromStorageString(chordText), order };
        maxOrder = std::max(maxOrder, order);
    }

    const auto storedNextOrder =
        static_cast<std::uint64_t>(root->value(QStringLiteral("nextOrder")).toDouble(static_cast<double>(maxOrder + 1)));
    bindings.setOverrides(std::move(overrides), std::max(storedNextOrder, maxOrder + 1));
    return bindings;
}

bool HotkeyStore::save(const HotkeyBindings& bindings) const
{
    QJsonArray overrideArray;
    for (const auto& [id, override_] : bindings.overrides())
    {
        QJsonObject json;
        json[QStringLiteral("id")] = QString::fromStdString(id);
        json[QStringLiteral("chord")] = override_.chord.toStorageString();
        json[QStringLiteral("order")] = static_cast<double>(override_.order);
        overrideArray.append(json);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("nextOrder")] = static_cast<double>(bindings.nextOrder());
    root[QStringLiteral("overrides")] = overrideArray;

    return JsonFileStore::trySave(m_hotkeysFilePath, root, "HotkeyStore");
}
