#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "FavoriteItem.h"

// A named, collapsible group of FavoriteItem entries (Architecture.md §14.35) -- one flat level,
// no nested categories. Pure C++ value type, no Qt/I/O; owned by Favorites' top-level entries().
class FavoriteCategory
{
public:
    using Id = std::int64_t;

    FavoriteCategory(Id id, std::string name)
        : m_id(id)
        , m_name(std::move(name))
    {
    }

    Id id() const noexcept { return m_id; }
    const std::string& name() const noexcept { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    std::vector<FavoriteItem>& items() noexcept { return m_items; }
    const std::vector<FavoriteItem>& items() const noexcept { return m_items; }

private:
    Id m_id;
    std::string m_name;
    std::vector<FavoriteItem> m_items;
};
