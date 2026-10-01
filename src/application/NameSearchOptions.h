#pragma once

// Narrows how FileNavigationUseCase::filterByName interprets its query, for the quick Search tab's
// Match Case/Whole Word/Exact Name/Match Regex toggles (Architecture.md §14.44). Plain C++ struct,
// no Qt -- same posture as SearchCriteria.h. Default-constructed == today's Contains/
// substring-or-wildcard, case-insensitive behavior, which is exactly what the Advanced Search pane
// (SearchCriteria/filterByCriteria) keeps using.
enum class NameMatchMode
{
    Contains,
    WholeWord,
    ExactName,
    Regex,
};

struct NameSearchOptions
{
    NameMatchMode mode = NameMatchMode::Contains;
    bool caseSensitive = false;

    // Quick Search tab's "skip git-ignored files/folders" toggle (Architecture.md §14.45).
    // Orthogonal to mode/caseSensitive: unlike those, it changes what gets *scanned* (pruning the
    // recursive walk), not just how an already-scanned candidate list is filtered -- see
    // TabViewModel::setSearchOptions, which forces a rescan specifically when this changes.
    bool skipGitIgnored = false;
};
