#pragma once

#include <string>

// Plain "Contains" substring matcher for Content Search's main query, matched against file
// *content* (Architecture.md §14.104) -- the one matcher mode GlobMatch/WholeWordMatch/RegexMatch
// didn't already name. No glob/wildcard expansion here: '*'/'?' are literal. Content Search's
// Include/Exclude fields match file *paths* instead and go through GlobMatch's wildcard expansion
// (see ContentSearchUseCase::pathMatches), same as filename search's own "Contains" mode.
namespace SubstringMatch
{
    // True if queryUtf8 occurs anywhere in textUtf8. Both arguments are UTF-8. caseSensitive=false
    // (default) folds both sides via UnicodeCaseFold before comparing. Empty query never matches.
    bool matches(const std::string& queryUtf8, const std::string& textUtf8, bool caseSensitive = false);
}
