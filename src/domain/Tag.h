#pragma once

#include <cstdint>
#include <string>

#include "Result.h"

// Entity representing a user-defined tag. Immutable value type — editing a tag's name/color
// is modeled as creating a new validated Tag with the same id, not in-place mutation, so a
// Tag is never observable in a partially-invalid state.
class Tag
{
public:
    using Id = std::int64_t;

    // Reserved id for a tag not yet persisted (e.g. before ITagRepository assigns a real id).
    static constexpr Id kUnassignedId = 0;

    static Result<Tag> create(Id id, std::string name, std::string hexColor);

    Id id() const noexcept { return m_id; }
    const std::string& name() const noexcept { return m_name; }
    const std::string& hexColor() const noexcept { return m_hexColor; }

    friend bool operator==(const Tag& lhs, const Tag& rhs) noexcept;

private:
    Tag(Id id, std::string name, std::string hexColor);

    Id m_id;
    std::string m_name;
    std::string m_hexColor;
};

inline bool operator!=(const Tag& lhs, const Tag& rhs) noexcept
{
    return !(lhs == rhs);
}
