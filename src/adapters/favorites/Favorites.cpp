#include "Favorites.h"

#include <algorithm>
#include <utility>

#include "PathContainment.h"

namespace
{
    // Shared "find the element with this id, then swap it with its neighbor" logic behind
    // moveEntryUp/moveEntryDown and moveItemUpWithinCategory/moveItemDownWithinCategory, which were
    // otherwise copy-pasted mirror images of each other differing only in swap direction.
    template <typename Container, typename IdOf>
    void moveById(Container& container, Favorites::Id id, bool up, IdOf idOf)
    {
        for (std::size_t i = 0; i < container.size(); ++i)
        {
            if (idOf(container[i]) != id)
            {
                continue;
            }

            if (up)
            {
                if (i > 0)
                {
                    std::swap(container[i], container[i - 1]);
                }
            }
            else if (i + 1 < container.size())
            {
                std::swap(container[i], container[i + 1]);
            }
            return;
        }
    }

    Favorites::Id entryId(const Favorites::Entry& entry)
    {
        return std::visit([](const auto& value) -> Favorites::Id { return value.id(); }, entry);
    }
}

Favorites::Id Favorites::addItem(const std::filesystem::path& path)
{
    if (const auto existing = findExistingItemId(path))
    {
        return *existing;
    }

    const Id id = allocateId();
    m_entries.emplace_back(FavoriteItem(id, path));
    return id;
}

Favorites::Id Favorites::addCategory(const std::string& name)
{
    const Id id = allocateId();
    m_entries.emplace_back(FavoriteCategory(id, name));
    return id;
}

void Favorites::renameCategory(Id categoryId, const std::string& name)
{
    for (Entry& entry : m_entries)
    {
        if (auto* category = std::get_if<FavoriteCategory>(&entry); category && category->id() == categoryId)
        {
            category->setName(name);
            return;
        }
    }
}

void Favorites::removeEntry(Id id)
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (const auto* item = std::get_if<FavoriteItem>(&*it); item && item->id() == id)
        {
            m_entries.erase(it);
            return;
        }

        if (const auto* category = std::get_if<FavoriteCategory>(&*it); category && category->id() == id)
        {
            std::vector<FavoriteItem> orphaned = category->items();
            m_entries.erase(it);
            for (FavoriteItem& orphan : orphaned)
            {
                m_entries.emplace_back(std::move(orphan));
            }
            return;
        }
    }
}

void Favorites::removeItemFromCategory(Id categoryId, Id itemId)
{
    for (Entry& entry : m_entries)
    {
        auto* category = std::get_if<FavoriteCategory>(&entry);
        if (!category || category->id() != categoryId)
        {
            continue;
        }

        std::vector<FavoriteItem>& items = category->items();
        items.erase(std::remove_if(items.begin(), items.end(), [itemId](const FavoriteItem& item) { return item.id() == itemId; }),
                    items.end());
        return;
    }
}

void Favorites::moveItemToCategory(Id itemId, std::optional<Id> categoryId)
{
    if (categoryId)
    {
        const bool categoryExists = std::any_of(m_entries.begin(), m_entries.end(), [categoryId](const Entry& entry) {
            const auto* category = std::get_if<FavoriteCategory>(&entry);
            return category && category->id() == *categoryId;
        });
        // Checked before extracting -- an extract-then-fail-to-find-target would otherwise
        // silently drop the item.
        if (!categoryExists)
        {
            return;
        }
    }

    std::optional<FavoriteItem> extracted = extractItem(itemId);
    if (!extracted)
    {
        return;
    }

    if (!categoryId)
    {
        m_entries.emplace_back(std::move(*extracted));
        return;
    }

    for (Entry& entry : m_entries)
    {
        if (auto* category = std::get_if<FavoriteCategory>(&entry); category && category->id() == *categoryId)
        {
            category->items().emplace_back(std::move(*extracted));
            return;
        }
    }
}

void Favorites::moveEntryUp(Id id)
{
    moveById(m_entries, id, /*up=*/true, entryId);
}

void Favorites::moveEntryDown(Id id)
{
    moveById(m_entries, id, /*up=*/false, entryId);
}

void Favorites::moveItemUpWithinCategory(Id categoryId, Id itemId)
{
    for (Entry& entry : m_entries)
    {
        auto* category = std::get_if<FavoriteCategory>(&entry);
        if (!category || category->id() != categoryId)
        {
            continue;
        }

        moveById(category->items(), itemId, /*up=*/true, [](const FavoriteItem& item) { return item.id(); });
        return;
    }
}

void Favorites::moveItemDownWithinCategory(Id categoryId, Id itemId)
{
    for (Entry& entry : m_entries)
    {
        auto* category = std::get_if<FavoriteCategory>(&entry);
        if (!category || category->id() != categoryId)
        {
            continue;
        }

        moveById(category->items(), itemId, /*up=*/false, [](const FavoriteItem& item) { return item.id(); });
        return;
    }
}

std::optional<Favorites::Id> Favorites::findExistingItemId(const std::filesystem::path& path) const
{
    for (const Entry& entry : m_entries)
    {
        if (const auto* item = std::get_if<FavoriteItem>(&entry))
        {
            if (pathsEqualCaseInsensitive(item->path(), path))
            {
                return item->id();
            }
            continue;
        }

        const auto* category = std::get_if<FavoriteCategory>(&entry);
        for (const FavoriteItem& categoryItem : category->items())
        {
            if (pathsEqualCaseInsensitive(categoryItem.path(), path))
            {
                return categoryItem.id();
            }
        }
    }
    return std::nullopt;
}

std::optional<FavoriteItem> Favorites::extractItem(Id itemId)
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (const auto* item = std::get_if<FavoriteItem>(&*it); item && item->id() == itemId)
        {
            FavoriteItem extracted = *item;
            m_entries.erase(it);
            return extracted;
        }
    }

    for (Entry& entry : m_entries)
    {
        auto* category = std::get_if<FavoriteCategory>(&entry);
        if (!category)
        {
            continue;
        }

        std::vector<FavoriteItem>& items = category->items();
        const auto it = std::find_if(items.begin(), items.end(), [itemId](const FavoriteItem& item) { return item.id() == itemId; });
        if (it != items.end())
        {
            FavoriteItem extracted = *it;
            items.erase(it);
            return extracted;
        }
    }

    return std::nullopt;
}
