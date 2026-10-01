#pragma once

#include <string>

#include "Result.h"

// Regex name matcher for the quick Search tab's Match Regex toggle (Architecture.md §14.44).
namespace RegexMatch
{
    // True if patternUtf8, compiled as a std::wregex, matches anywhere in textUtf8. Both are UTF-8,
    // converted to native wide strings via std::filesystem::path (UTF-16 on Windows, UTF-32 on
    // Linux) -- reusing the project's existing portable UTF-8<->wide conversion. caseSensitive=false
    // (default) compiles with std::regex_constants::icase. An invalid pattern (std::regex_error from
    // std::wregex's constructor) is caught at this boundary and returned as Result::failure rather
    // than propagating -- the pattern is live search-box input, not a programmer constant.
    //
    // Known trade-off: on Windows a codepoint outside the BMP is a UTF-16 surrogate pair, which a
    // `.`/character-class regex construct may not treat as a single "character" -- acceptable for a
    // search box, not claimed as fully Unicode-correct (same category of trade-off UnicodeCaseFold
    // already documents for itself).
    Result<bool> matches(const std::string& patternUtf8, const std::string& textUtf8, bool caseSensitive = false);
}
