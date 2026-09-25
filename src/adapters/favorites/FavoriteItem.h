#pragma once

#include <cstdint>
#include <filesystem>

// One favorited file or folder (Architecture.md §14.35), owned either at Favorites' top level or
// inside a FavoriteCategory. Pure C++ value type, no Qt/I/O.
class FavoriteItem
{
public:
    using Id = std::int64_t;

    FavoriteItem(Id id, std::filesystem::path path)
        : m_id(id)
        , m_path(std::move(path))
    {
    }

    Id id() const noexcept { return m_id; }
    const std::filesystem::path& path() const noexcept { return m_path; }

    // Derived at render time rather than stored, same posture as FileNode::name(): falls back to
    // the full path when filename() is empty (e.g. a drive root like "C:\").
    std::filesystem::path displayName() const
    {
        const std::filesystem::path name = m_path.filename();
        return name.empty() ? m_path : name;
    }

private:
    Id m_id;
    std::filesystem::path m_path;
};
