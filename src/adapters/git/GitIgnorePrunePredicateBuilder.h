#pragma once

#include <filesystem>
#include <functional>

class GitStatusUseCase;

// Builds the shouldPrune predicate a recursive FileNavigationUseCase::listDirectoryRecursive walk
// passes to skip everything .gitignore would exclude under `root`, plus root/".git" itself (git's
// own status machinery never reports ".git" as "ignored" since it isn't part of the working tree
// at all, but a user skipping ignored content clearly doesn't want ".git"'s internals in results
// either). Tolerates ignoredPaths() failure/empty (not a repository) by returning an
// always-false predicate, i.e. an unrestricted scan.
//
// Extracted out of AdvancedSearchWorker::buildGitIgnorePrunePredicate (Architecture.md §14.45) so
// ContentSearchWorker (Architecture.md §14.104) can reuse the exact same logic instead of a second
// hand-copy.
std::function<bool(const std::filesystem::path&)> buildGitIgnorePrunePredicate(GitStatusUseCase& gitStatusUseCase,
                                                                                 const std::filesystem::path& root);
