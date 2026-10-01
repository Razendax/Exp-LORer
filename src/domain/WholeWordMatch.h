#pragma once

#include <string>

// "Whole word" name matcher for the quick Search tab's Whole Word toggle (Architecture.md §14.44).
// Both arguments are UTF-8, matched via decoded Unicode codepoints -- same posture as GlobMatch.
namespace WholeWordMatch
{
    // True if queryUtf8 occurs in textUtf8 at a position bounded on both sides (where present) by a
    // non-word codepoint -- so "File" matches "File Name"/"File-Name"/"File_Name" but not
    // "FileName". caseSensitive=false (default) folds both sides via UnicodeCaseFold before
    // comparing. Empty query never matches.
    bool matches(const std::string& queryUtf8, const std::string& textUtf8, bool caseSensitive = false);
}
