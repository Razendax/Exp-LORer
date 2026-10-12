#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "NameSearchOptions.h"
#include "Tag.h"

// Criteria for the advanced (criteria) search pane (Architecture.md §14.19). Plain C++ struct, no
// Qt — same posture as VirtualPaths.h/NativeTypes.h.
struct SearchCriteria
{
    std::string nameQuery;                       // substring or *?-glob; empty = no filter
    std::optional<std::uintmax_t> minSizeBytes;   // nullopt = no lower bound
    std::optional<std::uintmax_t> maxSizeBytes;   // nullopt = no upper bound

    // AND semantics, mirrors ITagRepository::findFilesWithAllTags; empty = no tag filter.
    std::vector<Tag::Id> tagIds;

    // When false (default), tagIds match exactly, preserving today's behavior. When true, each
    // selected tag also matches its "/"-nested children (Architecture.md §14.87), e.g. selecting
    // "game" also matches "game/action".
    bool tagIncludeChildTags = false;

    // Match Case/Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles for nameQuery
    // (Architecture.md §14.56), same type the Search tab's NameSearchOptions already uses.
    // Default-constructed keeps the pre-existing Contains/case-insensitive behavior.
    NameSearchOptions nameOptions;
};
