#include "WholeWordMatch.h"

#include <algorithm>
#include <vector>

#include "UnicodeCaseFold.h"

namespace
{
    // ASCII letter/digit, or any codepoint above 0x7F -- same "good enough for real-world names"
    // heuristic UnicodeCaseFold's own doc comment already accepts, not a full Unicode word-char
    // table.
    bool isWordChar(char32_t c)
    {
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') || c > 0x7F;
    }
}

bool WholeWordMatch::matches(const std::string& queryUtf8, const std::string& textUtf8, bool caseSensitive)
{
    if (queryUtf8.empty())
    {
        return false;
    }

    std::vector<char32_t> query = UnicodeCaseFold::decodeUtf8(queryUtf8);
    std::vector<char32_t> text = UnicodeCaseFold::decodeUtf8(textUtf8);

    if (!caseSensitive)
    {
        for (char32_t& c : query) c = UnicodeCaseFold::foldCodepoint(c);
        for (char32_t& c : text) c = UnicodeCaseFold::foldCodepoint(c);
    }

    if (query.empty() || query.size() > text.size())
    {
        return false;
    }

    for (size_t start = 0; start + query.size() <= text.size(); ++start)
    {
        if (!std::equal(query.begin(), query.end(), text.begin() + static_cast<std::ptrdiff_t>(start)))
        {
            continue;
        }

        const bool boundaryBefore = start == 0 || !isWordChar(text[start - 1]);
        const size_t end = start + query.size();
        const bool boundaryAfter = end == text.size() || !isWordChar(text[end]);

        if (boundaryBefore && boundaryAfter)
        {
            return true;
        }
    }

    return false;
}
