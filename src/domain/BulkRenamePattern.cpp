#include "BulkRenamePattern.h"

#include <filesystem>
#include <regex>
#include <vector>

#include "UnicodeCaseFold.h"

namespace
{
    std::wstring toWide(const std::string& utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())).wstring();
    }

    std::string toUtf8(const std::wstring& wide)
    {
        const std::u8string encoded = std::filesystem::path(wide).u8string();
        return std::string(encoded.begin(), encoded.end());
    }

    // Plain (non-regex) find/replace: every non-overlapping occurrence of findText is replaced,
    // scanning left to right -- same "global replace" semantics as std::regex_replace's default,
    // kept consistent between the two modes. Matching is by decoded codepoint, case-folded via
    // UnicodeCaseFold when matchCase is false; replaceText itself is inserted literally, never
    // folded.
    std::string plainReplace(const std::string& text, const std::string& findText, const std::string& replaceText,
                              bool matchCase)
    {
        const std::vector<char32_t> textCp = UnicodeCaseFold::decodeUtf8(text);
        const std::vector<char32_t> findCp = UnicodeCaseFold::decodeUtf8(findText);
        if (findCp.empty())
        {
            return text;
        }
        const std::vector<char32_t> replaceCp = UnicodeCaseFold::decodeUtf8(replaceText);

        const auto codepointsEqual = [matchCase](char32_t a, char32_t b) {
            if (matchCase)
            {
                return a == b;
            }
            return UnicodeCaseFold::foldCodepoint(a) == UnicodeCaseFold::foldCodepoint(b);
        };

        std::vector<char32_t> result;
        result.reserve(textCp.size());

        std::size_t i = 0;
        while (i < textCp.size())
        {
            bool matched = i + findCp.size() <= textCp.size();
            for (std::size_t k = 0; matched && k < findCp.size(); ++k)
            {
                matched = codepointsEqual(textCp[i + k], findCp[k]);
            }

            if (matched)
            {
                result.insert(result.end(), replaceCp.begin(), replaceCp.end());
                i += findCp.size();
            }
            else
            {
                result.push_back(textCp[i]);
                ++i;
            }
        }

        return UnicodeCaseFold::encodeUtf8(result);
    }

    Result<std::string> regexReplace(const std::string& text, const std::string& pattern, const std::string& replacement,
                                      bool matchCase)
    {
        auto flags = std::regex_constants::ECMAScript;
        if (!matchCase)
        {
            flags |= std::regex_constants::icase;
        }

        try
        {
            const std::wregex regex(toWide(pattern), flags);
            const std::wstring replaced = std::regex_replace(toWide(text), regex, toWide(replacement));
            return Result<std::string>::success(toUtf8(replaced));
        }
        catch (const std::regex_error&)
        {
            return Result<std::string>::failure(Error(ErrorCode::InvalidArgument, "Invalid regex pattern"));
        }
    }

    // Inverse of UnicodeCaseFold::foldCodepoint, covering the same scripts (ASCII, Latin-1
    // Supplement, Latin Extended-A, Greek, Cyrillic) so Upper/Title transforms round-trip with
    // Lower over the same non-ASCII range this project already commits to supporting
    // (Architecture.md's non-ASCII hard requirement). Identity for anything outside that range, or
    // anything already uppercase.
    char32_t toUpperCodepoint(char32_t c)
    {
        if (c >= U'a' && c <= U'z')
        {
            return c - U'a' + U'A';
        }

        // Latin-1 Supplement lowercase block, except U+00F7 (division sign, not a letter).
        if (c >= 0x00E0 && c <= 0x00FE && c != 0x00F7)
        {
            return c - 0x20;
        }

        // Latin Extended-A: mirrors UnicodeCaseFold::foldCodepoint's pairing exactly, inverted.
        if (c >= 0x0101 && c <= 0x0137 && (c % 2) == 1)
        {
            return c - 1;
        }
        if (c >= 0x013A && c <= 0x0148 && (c % 2) == 0)
        {
            return c - 1;
        }
        if (c >= 0x014B && c <= 0x0177 && (c % 2) == 1)
        {
            return c - 1;
        }
        if (c == 0x00FF) // ÿ -> Ÿ
        {
            return 0x0178;
        }
        if (c >= 0x017A && c <= 0x017E && (c % 2) == 0)
        {
            return c - 1;
        }

        // Greek lowercase block; final sigma (ς) uppercases the same as medial sigma.
        if (c == 0x03C2)
        {
            return 0x03A3;
        }
        if (c >= 0x03B1 && c <= 0x03C9)
        {
            return c - 0x20;
        }

        // Cyrillic lowercase blocks.
        if (c >= 0x0450 && c <= 0x045F)
        {
            return c - 0x50;
        }
        if (c >= 0x0430 && c <= 0x044F)
        {
            return c - 0x20;
        }

        return c;
    }

    bool isWordCharacter(char32_t c)
    {
        return toUpperCodepoint(c) != c || UnicodeCaseFold::foldCodepoint(c) != c ||
               (c >= U'0' && c <= U'9');
    }

    std::string applyCaseTransform(const std::string& text, BulkRenamePattern::CaseTransform transform)
    {
        if (transform == BulkRenamePattern::CaseTransform::None)
        {
            return text;
        }

        std::vector<char32_t> codepoints = UnicodeCaseFold::decodeUtf8(text);

        switch (transform)
        {
            case BulkRenamePattern::CaseTransform::Upper:
                for (char32_t& c : codepoints)
                {
                    c = toUpperCodepoint(c);
                }
                break;
            case BulkRenamePattern::CaseTransform::Lower:
                for (char32_t& c : codepoints)
                {
                    c = UnicodeCaseFold::foldCodepoint(c);
                }
                break;
            case BulkRenamePattern::CaseTransform::Title:
            {
                bool atWordStart = true;
                for (char32_t& c : codepoints)
                {
                    if (!isWordCharacter(c))
                    {
                        atWordStart = true;
                        continue;
                    }
                    c = atWordStart ? toUpperCodepoint(c) : UnicodeCaseFold::foldCodepoint(c);
                    atWordStart = false;
                }
                break;
            }
            case BulkRenamePattern::CaseTransform::None:
                break;
        }

        return UnicodeCaseFold::encodeUtf8(codepoints);
    }

    // Zero-pads value's decimal representation to at least `digits` characters, preserving a
    // leading '-' for a negative counter outside the padded width.
    std::string zeroPadded(int value, int digits)
    {
        const std::string sign = value < 0 ? "-" : "";
        std::string numeric = std::to_string(value < 0 ? -static_cast<long long>(value) : value);
        while (static_cast<int>(numeric.size()) < digits)
        {
            numeric.insert(numeric.begin(), '0');
        }
        return sign + numeric;
    }
}

Result<std::string> BulkRenamePattern::apply(const std::string& originalStem, const std::string& originalExtension,
                                              int indexInBatch) const
{
    std::string stem = originalStem;

    if (!findText.empty())
    {
        if (useRegex)
        {
            auto replaced = regexReplace(stem, findText, replaceText, matchCase);
            if (replaced.hasError())
            {
                return Result<std::string>::failure(std::move(replaced).error());
            }
            stem = std::move(replaced).value();
        }
        else
        {
            stem = plainReplace(stem, findText, replaceText, matchCase);
        }
    }

    stem = applyCaseTransform(stem, caseTransform);

    if (counterEnabled)
    {
        const int counterValue = counterStart + indexInBatch * counterStep;
        const std::string counterText = zeroPadded(counterValue, counterDigits);
        stem = (counterPosition == CounterPosition::Prefix) ? (counterText + counterSeparator + stem)
                                                              : (stem + counterSeparator + counterText);
    }

    std::string extension = originalExtension;
    if (newExtension.has_value())
    {
        if (newExtension->empty())
        {
            extension.clear();
        }
        else
        {
            const bool hasLeadingDot = newExtension->front() == '.';
            extension = "." + (hasLeadingDot ? newExtension->substr(1) : *newExtension);
        }
    }

    return Result<std::string>::success(stem + extension);
}
