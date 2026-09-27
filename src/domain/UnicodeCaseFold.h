#pragma once

#include <string>
#include <vector>

// Shared UTF-8 decode + simple case-fold used by both GlobMatch (name matching) and
// PathContainment (case-insensitive path containment on Windows). Folds ASCII plus the scripts
// most likely to appear in real-world file/folder names (Architecture.md's non-ASCII hard
// requirement: accented Latin, Cyrillic, Greek) -- not a full Unicode case-folding table, so an
// exotic script's case pairs won't fold. Malformed UTF-8 bytes are dropped rather than rejected,
// same tolerance decodeUtf8's callers already have.
namespace UnicodeCaseFold
{
    std::vector<char32_t> decodeUtf8(const std::string& utf8);
    std::string encodeUtf8(const std::vector<char32_t>& codepoints);

    // Case-folds one Unicode codepoint (identity for anything outside the covered scripts).
    char32_t foldCodepoint(char32_t c);

    // Decodes, folds every codepoint, and re-encodes -- for whole-string case-insensitive
    // comparisons.
    std::string foldUtf8(const std::string& utf8);
}
