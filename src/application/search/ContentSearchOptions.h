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
    bool skipGitIgnored = false;

    // Empty = unset (no filtering by this field).
    std::string includePattern;
    TextMatchOptions includeOptions;
    std::string excludePattern;
    TextMatchOptions excludeOptions;
};
