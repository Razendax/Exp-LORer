#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Tag.h"

// Criteria for the advanced (criteria) search pane (Architecture.md §14.19). Plain C++ struct, no
// Qt — same posture as VirtualPaths.h/NativeTypes.h.
struct SearchCriteria
{
    std::string nameQuery;                       // substring, case-insensitive; empty = no filter
    std::optional<std::uintmax_t> minSizeBytes;   // nullopt = no lower bound
    std::optional<std::uintmax_t> maxSizeBytes;   // nullopt = no upper bound
    std::string extensionList;                    // "jpg, png"; empty = no filter

    // AND semantics, mirrors ITagRepository::findFilesWithAllTags; empty = no tag filter.
    std::vector<Tag::Id> tagIds;
};
