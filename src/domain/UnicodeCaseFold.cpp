#include "UnicodeCaseFold.h"

#include <type_traits>

#include "PathUtf8.h"

std::vector<char32_t> UnicodeCaseFold::decodeUtf8(const std::string& utf8)
{
    std::vector<char32_t> codepoints;
    codepoints.reserve(utf8.size());

    size_t i = 0;
    while (i < utf8.size())
    {
        const unsigned char lead = static_cast<unsigned char>(utf8[i]);
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
            if (i >= utf8.size() || (static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
            ++i;
        }

        if (valid)
        {
            codepoints.push_back(codepoint);
        }
    }

    return codepoints;
}

std::string UnicodeCaseFold::encodeUtf8(const std::vector<char32_t>& codepoints)
{
    std::string utf8;
    utf8.reserve(codepoints.size());

    for (const char32_t c : codepoints)
    {
        if (c <= 0x7F)
        {
            utf8.push_back(static_cast<char>(c));
        }
        else if (c <= 0x7FF)
        {
            utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else if (c <= 0xFFFF)
        {
            utf8.push_back(static_cast<char>(0xE0 | (c >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else
        {
            utf8.push_back(static_cast<char>(0xF0 | (c >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }

    return utf8;
}

char32_t UnicodeCaseFold::foldCodepoint(char32_t c)
{
    if (c >= U'A' && c <= U'Z')
    {
        return c - U'A' + U'a';
    }

    // Latin-1 Supplement uppercase block, except U+00D7 (multiplication sign, not a letter).
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)
    {
        return c + 0x20;
    }

    // Latin Extended-A: mostly alternating upper/lower codepoint pairs, with a few standalone
    // exceptions (0x0138, 0x0149, 0x017F have no case partner here).
    if (c >= 0x0100 && c <= 0x0136 && (c % 2) == 0)
    {
        return c + 1;
    }
    if (c >= 0x0139 && c <= 0x0147 && (c % 2) == 1)
    {
        return c + 1;
    }
    if (c >= 0x014A && c <= 0x0176 && (c % 2) == 0)
    {
        return c + 1;
    }
    if (c == 0x0178) // Ÿ -> ÿ (breaks the even/odd pairing above)
    {
        return 0x00FF;
    }
    if (c >= 0x0179 && c <= 0x017D && (c % 2) == 1)
    {
        return c + 1;
    }

    // Greek uppercase block (U+03A2 is unassigned); final sigma folds onto medial sigma.
    if (c >= 0x0391 && c <= 0x03A9 && c != 0x03A2)
    {
        return c + 0x20;
    }
    if (c == 0x03C2)
    {
        return 0x03C3;
    }

    // Cyrillic uppercase blocks.
    if (c >= 0x0400 && c <= 0x040F)
    {
        return c + 0x50;
    }
    if (c >= 0x0410 && c <= 0x042F)
    {
        return c + 0x20;
    }

    return c;
}

std::string UnicodeCaseFold::foldUtf8(const std::string& utf8)
{
    std::vector<char32_t> codepoints = decodeUtf8(utf8);
    for (char32_t& c : codepoints)
    {
        c = foldCodepoint(c);
    }
    return encodeUtf8(codepoints);
}

std::filesystem::path UnicodeCaseFold::foldPath(const std::filesystem::path& path)
{
    const std::string folded = foldUtf8(PathUtf8::toUtf8(path));
    return std::filesystem::path(std::u8string(folded.begin(), folded.end()));
}

std::wstring UnicodeCaseFold::foldWide(const std::wstring& wide)
{
    std::wstring folded = wide;
    for (wchar_t& c : folded)
    {
        c = static_cast<wchar_t>(foldCodepoint(static_cast<char32_t>(static_cast<std::make_unsigned_t<wchar_t>>(c))));
    }
    return folded;
}
