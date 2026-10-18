#pragma once

#include <filesystem>
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

    // Case-folds `path`'s text and rebuilds it as a path -- separators are untouched by folding,
    // so the rebuilt path still has the same components as the original. For Windows'
    // case-insensitive path comparisons/containment (shared by PathContainment and
    // FolderTreeModel).
    std::filesystem::path foldPath(const std::filesystem::path& path);

    // Case-folds a UTF-16 string one code unit at a time. Every codepoint foldCodepoint covers
    // (this header's core scripts) fits in one UTF-16 code unit, so this is equivalent to folding
    // full codepoints without a UTF-8 round trip -- for native wide path keys (e.g.
    // DirectoryWatchRegistry) where the extra round trip would be pure overhead.
    std::wstring foldWide(const std::wstring& wide);
}
