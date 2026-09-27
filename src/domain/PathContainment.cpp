#include "PathContainment.h"

#include "PathUtf8.h"
#include "UnicodeCaseFold.h"

namespace
{
    // Case-folds `path`'s text (see UnicodeCaseFold.h) and rebuilds it as a path, so containment
    // is computed on Windows' own case-insensitive terms -- separators are untouched by folding, so
    // the rebuilt path still has the same components as the original.
    std::filesystem::path caseFolded(const std::filesystem::path& path)
    {
        const std::string folded = UnicodeCaseFold::foldUtf8(PathUtf8::toUtf8(path));
        return std::filesystem::path(std::u8string(folded.begin(), folded.end()));
    }
}

bool isPathWithinOrEqual(const std::filesystem::path& candidate, const std::filesystem::path& root)
{
    const std::filesystem::path rel = caseFolded(candidate).lexically_relative(caseFolded(root));
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

bool pathsEqualCaseInsensitive(const std::filesystem::path& a, const std::filesystem::path& b)
{
    return caseFolded(a) == caseFolded(b);
}
