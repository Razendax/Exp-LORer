#pragma once

#include <string>

// Plain "Contains" substring matcher for Content Search's main query and its Include/Exclude
// fields (Architecture.md §14.104) -- the one matcher mode GlobMatch/WholeWordMatch/RegexMatch
// didn't already name, since filename search's "Contains" mode is folded into GlobMatch's
// wildcard expansion instead. No glob/wildcard expansion here: '*'/'?' are literal.
namespace SubstringMatch
{
    // True if queryUtf8 occurs anywhere in textUtf8. Both arguments are UTF-8. caseSensitive=false
    // (default) folds both sides via UnicodeCaseFold before comparing. Empty query never matches.
    bool matches(const std::string& queryUtf8, const std::string& textUtf8, bool caseSensitive = false);
}
