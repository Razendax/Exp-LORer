#pragma once

#include <string>

// Pure helper for the "/"-nested tag naming convention (Architecture.md §14.87): a tag named
// "game/action" is considered a child of "game" purely by string prefix -- there is no schema
// concept of hierarchy and the parent tag need not exist as its own row.
namespace TagHierarchy
{
    // True when `name` is `ancestorName` itself, or a descendant of it (i.e. `name` starts with
    // `ancestorName + "/"`). Case-insensitive via UnicodeCaseFold, matching the project's other
    // Unicode-safe name comparisons.
    bool isSelfOrDescendant(const std::string& name, const std::string& ancestorName);
}
