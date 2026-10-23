#pragma once

#include <string>

// Narrows how ContentSearchUseCase interprets its main query and Include/Exclude fields, for the
// new "Find in Files" left-panel tab (Architecture.md §14.104). Plain C++ struct, no Qt -- same
// posture as NameSearchOptions.h.
enum class TextMatchMode
{
    Contains,
    WholeWord,
    Regex,
};

struct TextMatchOptions
{
    TextMatchMode mode = TextMatchMode::Contains;
    bool caseSensitive = false;
};

struct ContentSearchOptions
{
    TextMatchOptions queryOptions;

    // Changes what gets *scanned* (pruning the recursive walk), not just how an already-scanned
    // candidate list is filtered -- same orthogonality NameSearchOptions::skipGitIgnored already
    // documents; see TabContentSearchViewModel::setOptions, which forces a rescan specifically
    // when this changes.
    //
    // Defaults to true (unlike NameSearchOptions::skipGitIgnored): content search reads every
    // surviving candidate's bytes off disk, not just its name, so an unfiltered scan over a typical
    // repo (build output, vcpkg_installed, etc., all usually git-ignored) is drastically more
    // expensive than the equivalent filename-only scan -- enough to make the feature look hung on a
    // real project with no included/explicit opt-out.
    bool skipGitIgnored = true;

    // Empty = unset (no filtering by this field). Matched against each candidate's path relative to
    // the search root; in the default Contains mode, '*'/'?' are glob wildcards (same convention as
    // filename search's own Contains mode) rather than literal characters -- see
    // ContentSearchUseCase::pathMatches.
    std::string includePattern;
    TextMatchOptions includeOptions;
    std::string excludePattern;
    TextMatchOptions excludeOptions;
};
