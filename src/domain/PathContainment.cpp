#include "PathContainment.h"

bool isPathWithinOrEqual(const std::filesystem::path& candidate, const std::filesystem::path& root)
{
    const std::filesystem::path rel = candidate.lexically_relative(root);
    if (rel.empty())
    {
        // Unrelated roots (e.g. different drives) -- lexically_relative can't express one in terms
        // of the other.
        return false;
    }

    if (rel == ".")
    {
        return true;
    }

    // Component-wise check, not a substring search: a first component of ".." means `candidate`
    // climbs out of `root` before descending back in (e.g. root/../root-sibling).
    return *rel.begin() != "..";
}
