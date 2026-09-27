#pragma once

#include <string>

// Full-string, case-insensitive (see UnicodeCaseFold.h for how far that extends) `*`/`?` glob
// matcher shared by §14.29's FileNamePattern (decoration rules) and
// FileNavigationUseCase::filterByName (advanced search). Both arguments are UTF-8; matched via
// decoded Unicode codepoints so `?` consumes one *character*, not one UTF-8 byte. Malformed UTF-8
// bytes are dropped, not rejected.
namespace GlobMatch
{
    // '*' matches any run of characters, including none; '?' matches exactly one codepoint.
    bool matches(const std::string& patternUtf8, const std::string& textUtf8);
}
