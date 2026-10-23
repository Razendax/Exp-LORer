#include "GitIgnorePrunePredicateBuilder.h"

#include <memory>
#include <unordered_set>

#include "GitStatusUseCase.h"
#include "PathHash.h"

std::function<bool(const std::filesystem::path&)> buildGitIgnorePrunePredicate(GitStatusUseCase& gitStatusUseCase,
                                                                                 const std::filesystem::path& root)
{
    auto ignoredResult = gitStatusUseCase.ignoredPaths(root);
    auto ignoredSet = std::make_shared<std::unordered_set<std::filesystem::path, PathHash>>();
    if (ignoredResult.hasValue())
    {
        ignoredSet->insert(ignoredResult.value().begin(), ignoredResult.value().end());
    }
    // Not reported as "ignored" by git's own status machinery (it isn't part of the working tree
    // at all), but a user skipping git-ignored content clearly doesn't want ".git"'s internals in
    // results either.
    ignoredSet->insert(root / ".git");

    return [ignoredSet](const std::filesystem::path& path) { return ignoredSet->count(path) != 0; };
}
