#include "Tag.h"

#include <algorithm>
#include <cctype>

namespace
{
    bool isValidHexColor(const std::string& hexColor)
    {
        if (hexColor.size() != 7 || hexColor[0] != '#')
        {
            return false;
        }

        return std::all_of(hexColor.begin() + 1, hexColor.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        });
    }
}

Result<Tag> Tag::create(Id id, std::string name, std::string hexColor)
{
    if (name.empty())
    {
        return Result<Tag>::failure(Error(ErrorCode::InvalidArgument, "Tag name must not be empty"));
    }

    if (!isValidHexColor(hexColor))
    {
        return Result<Tag>::failure(
            Error(ErrorCode::InvalidArgument, "Tag hexColor must be in \"#RRGGBB\" format"));
    }

    return Result<Tag>::success(Tag(id, std::move(name), std::move(hexColor)));
}

Tag::Tag(Id id, std::string name, std::string hexColor)
    : m_id(id)
    , m_name(std::move(name))
    , m_hexColor(std::move(hexColor))
{
}

bool operator==(const Tag& lhs, const Tag& rhs) noexcept
{
    return lhs.m_id == rhs.m_id && lhs.m_name == rhs.m_name && lhs.m_hexColor == rhs.m_hexColor;
}
