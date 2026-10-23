#include "SubstringMatch.h"

#include "UnicodeCaseFold.h"

bool SubstringMatch::matches(const std::string& queryUtf8, const std::string& textUtf8, bool caseSensitive)
{
    if (queryUtf8.empty())
    {
        return false;
    }

    if (caseSensitive)
    {
        return textUtf8.find(queryUtf8) != std::string::npos;
    }

    return UnicodeCaseFold::foldUtf8(textUtf8).find(UnicodeCaseFold::foldUtf8(queryUtf8)) != std::string::npos;
}
