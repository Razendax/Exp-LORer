#include "GlobMatch.h"

#include <vector>

#include "UnicodeCaseFold.h"

namespace
{
    // Standard greedy-with-backtrack `*`/`?` glob matcher (iterative, O(pattern+text) amortized),
    // operating on already-decoded codepoints so it is agnostic to UTF-8 byte widths.
    bool globMatch(const std::vector<char32_t>& pattern, const std::vector<char32_t>& text)
    {
        size_t p = 0;
        size_t t = 0;
        size_t starP = pattern.size() + 1;  // sentinel: "no star seen yet"
        size_t starT = 0;

        while (t < text.size())
        {
            if (p < pattern.size() &&
                (pattern[p] == U'?' || UnicodeCaseFold::foldCodepoint(pattern[p]) == UnicodeCaseFold::foldCodepoint(text[t])))
            {
                ++p;
                ++t;
            }
            else if (p < pattern.size() && pattern[p] == U'*')
            {
                starP = p;
                starT = t;
                ++p;
            }
            else if (starP <= pattern.size())
            {
                p = starP + 1;
                ++starT;
                t = starT;
            }
            else
            {
                return false;
            }
        }

        while (p < pattern.size() && pattern[p] == U'*')
        {
            ++p;
        }
        return p == pattern.size();
    }
}

bool GlobMatch::matches(const std::string& patternUtf8, const std::string& textUtf8)
{
    return globMatch(UnicodeCaseFold::decodeUtf8(patternUtf8), UnicodeCaseFold::decodeUtf8(textUtf8));
}
