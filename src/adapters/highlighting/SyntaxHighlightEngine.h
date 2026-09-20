#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Language.h"

// One highlighted token span, byte-offset into the source text that was passed to highlight()
// (Architecture.md §14.26). captureName is exactly the name tree-sitter's query gave that node
// (e.g. "keyword", "string.escape") -- never normalized, so HighlightTheme/SettingsDialog see the
// same granularity the language's own highlights.scm author intended.
struct HighlightSpan
{
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    std::string captureName;
};

// Owns one tree-sitter TSParser + compiled TSQuery per Language (lazy-built on first use), driving
// real syntax highlighting for the Preview panel's text view (Architecture.md §14.26). Pure logic --
// no Qt/SQLite/FFmpeg dependency except the one resource-loading function in the .cpp that reads a
// language's bundled highlights.scm out of the app's Qt resources (the only Qt-flavored corner of
// this module). GTest-covered directly (tests/adapters/SyntaxHighlightEngineTest.cpp), no Qt/disk
// touched by the tests themselves.
class SyntaxHighlightEngine
{
public:
    SyntaxHighlightEngine();
    ~SyntaxHighlightEngine();

    SyntaxHighlightEngine(const SyntaxHighlightEngine&) = delete;
    SyntaxHighlightEngine& operator=(const SyntaxHighlightEngine&) = delete;

    // Parses `source` fresh each call (preview text is capped and loaded once per file, never
    // edited -- Architecture.md §14.26) and returns every capture from that language's query, in
    // tree-sitter's own match order. An unrecognized/malformed query for `language` yields an empty
    // result rather than throwing.
    std::vector<HighlightSpan> highlight(Language language, std::string_view source);

    // Exactly the capture names `language`'s own bundled highlights.scm defines (e.g. Css has no
    // "function" capture) -- SettingsDialog lists only these per language, never a hardcoded
    // universal token list.
    const std::vector<std::string>& captureNamesFor(Language language);

private:
    struct LanguageContext;

    LanguageContext& contextFor(Language language);

    std::map<Language, std::unique_ptr<LanguageContext>> m_contexts;
};
