#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "FavoriteCategory.h"
#include "FavoriteItem.h"

// Ordered top-level list of favorite items and categories (Architecture.md §14.35): a persisted
// user preference like FileDecorationRules, mutated in place and persisted via FavoritesStore/
// FavoritesViewModel. Pure C++ mutation API -- no Qt, no I/O.
class Favorites
{
public:
    using Id = std::int64_t;
    using Entry = std::variant<FavoriteItem, FavoriteCategory>;

    const std::vector<Entry>& entries() const noexcept { return m_entries; }
    void setEntries(std::vector<Entry> entries) { m_entries = std::move(entries); }

    // Highest id handed out so far, persisted alongside entries (FavoritesStore) so ids stay
    // monotonic across a load without needing to rescan every entry for the current maximum.
    Id nextId() const noexcept { return m_nextId; }
    void setNextId(Id nextId) noexcept { m_nextId = nextId; }

    // Adds `path` as a new top-level, uncategorized favorite -- unless it's already favorited
    // anywhere (any category or top-level), in which case the existing entry's id is returned and
    // nothing changes (dedup).
    Id addItem(const std::filesystem::path& path);

    Id addCategory(const std::string& name);
    void renameCategory(Id categoryId, const std::string& name);

    // Removes a top-level item or a category by id. Removing a category flattens its items back
    // to top-level -- appended at the end, in their prior relative order -- rather than deleting
    // them.
    void removeEntry(Id id);

    void removeItemFromCategory(Id categoryId, Id itemId);

    // nullopt moves the item to top-level (appended at the end); otherwise moves it into the
    // given category (appended at the end of that category's items). No-op if itemId or the
    // target category doesn't exist.
    void moveItemToCategory(Id itemId, std::optional<Id> categoryId);

    // Reorders among top-level siblings (items and categories share one ordered sequence).
    void moveEntryUp(Id id);
    void moveEntryDown(Id id);

    void moveItemUpWithinCategory(Id categoryId, Id itemId);
    void moveItemDownWithinCategory(Id categoryId, Id itemId);

private:
    Id allocateId() { return m_nextId++; }

    // Finds `path` among every top-level item and every category's items -- dedup across the
    // whole structure, not just top-level.
    std::optional<Id> findExistingItemId(const std::filesystem::path& path) const;

    // Removes the item with `itemId` from wherever it currently lives (top-level or any category)
    // and returns it, or std::nullopt if no such item exists.
    std::optional<FavoriteItem> extractItem(Id itemId);

    std::vector<Entry> m_entries;
    Id m_nextId = 1;
};
