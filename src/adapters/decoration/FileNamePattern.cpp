#include "FileNamePattern.h"

#include <vector>

namespace
{
    // Decodes UTF-8 bytes to Unicode codepoints. Malformed lead/continuation bytes are dropped
    // rather than aborting the match -- a decoration rule failing to match a name with unusual
    // encoding is a cosmetic miss, not something worth crashing or erroring over.
    std::vector<char32_t> decodeUtf8(const std::string& text)
    {
        std::vector<char32_t> codepoints;
        codepoints.reserve(text.size());

        size_t i = 0;
        while (i < text.size())
        {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            char32_t codepoint = 0;
            size_t extraBytes = 0;

            if ((lead & 0x80) == 0)
            {
                codepoint = lead;
                extraBytes = 0;
            }
            else if ((lead & 0xE0) == 0xC0)
            {
                codepoint = lead & 0x1F;
                extraBytes = 1;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                codepoint = lead & 0x0F;
                extraBytes = 2;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                codepoint = lead & 0x07;
                extraBytes = 3;
            }
            else
            {
                ++i;
                continue;
            }

            ++i;
            bool valid = true;
            for (size_t k = 0; k < extraBytes; ++k)
            {
                if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
                ++i;
            }

            if (valid)
            {
                codepoints.push_back(codepoint);
            }
        }

        return codepoints;
    }

    char32_t asciiFold(char32_t c)
    {
        if (c >= U'A' && c <= U'Z')
        {
            return c - U'A' + U'a';
        }
        return c;
    }

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
            if (p < pattern.size() && (pattern[p] == U'?' || asciiFold(pattern[p]) == asciiFold(text[t])))
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

FileNamePattern::FileNamePattern(std::string pattern)
{
    if (!pattern.empty() && pattern.front() == '#')
    {
        m_appliesToFolders = true;
        pattern.erase(pattern.begin());
    }
    m_glob = std::move(pattern);
}

bool FileNamePattern::matches(const std::string& nameUtf8, bool isDirectory) const
{
    if (isDirectory != m_appliesToFolders)
    {
        return false;
    }

    return globMatch(decodeUtf8(m_glob), decodeUtf8(nameUtf8));
}
