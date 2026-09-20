#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "LanguageRegistry.h"
#include "SyntaxHighlightEngine.h"

namespace
{
    // True if some span in `spans` has `captureName` and covers exactly `expectedText` within
    // `source` -- the same "does the query actually fire on real code" check for every language.
    bool hasSpanCovering(const std::vector<HighlightSpan>& spans, const std::string& source, const std::string& captureName,
                         const std::string& expectedText)
    {
        return std::any_of(spans.begin(), spans.end(), [&](const HighlightSpan& span) {
            return span.captureName == captureName && source.substr(span.byteOffset, span.byteLength) == expectedText;
        });
    }
}

TEST(LanguageRegistry, MapsKnownExtensionsToTheirLanguage)
{
    EXPECT_EQ(LanguageRegistry::languageForExtension(".c"), Language::C);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".cpp"), Language::Cpp);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".h"), Language::Cpp);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".hpp"), Language::Cpp);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".cs"), Language::CSharp);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".py"), Language::Python);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".js"), Language::JavaScript);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".ts"), Language::TypeScript);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".json"), Language::Json);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".html"), Language::Html);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".htm"), Language::Html);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".css"), Language::Css);
}

TEST(LanguageRegistry, ReturnsNulloptForExtensionsOutOfScope)
{
    // Architecture.md §14.26 "Not Built": these stay plain, unhighlighted text.
    EXPECT_EQ(LanguageRegistry::languageForExtension(".md"), std::nullopt);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".txt"), std::nullopt);
    EXPECT_EQ(LanguageRegistry::languageForExtension(".yaml"), std::nullopt);
    EXPECT_EQ(LanguageRegistry::languageForExtension(""), std::nullopt);
}

TEST(SyntaxHighlightEngine, HighlightsCKeywordAndComment)
{
    SyntaxHighlightEngine engine;
    const std::string source = "int main() {\n    return 0; // done\n}\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::C, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "return"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "comment", "// done"));
}

TEST(SyntaxHighlightEngine, HighlightsCppOwnKeywordsOnTopOfInheritedCKeywords)
{
    SyntaxHighlightEngine engine;
    const std::string source = "class Foo {\npublic:\n    int bar() { return 1; }\n};\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::Cpp, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "class"));   // tree-sitter-cpp's own addition
    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "public"));  // tree-sitter-cpp's own addition
    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "return"));  // inherited from tree-sitter-c
}

TEST(SyntaxHighlightEngine, HighlightsCSharpKeywordAndComment)
{
    SyntaxHighlightEngine engine;
    const std::string source = "class Foo {\n    // hi\n    int Bar() { return 1; }\n}\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::CSharp, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "class"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "return"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "comment", "// hi"));
}

TEST(SyntaxHighlightEngine, HighlightsPythonKeywordAndComment)
{
    SyntaxHighlightEngine engine;
    const std::string source = "def add(a, b):\n    # sum\n    return a + b\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::Python, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "def"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "return"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "comment", "# sum"));
}

TEST(SyntaxHighlightEngine, HighlightsJavaScriptKeywordAndComment)
{
    SyntaxHighlightEngine engine;
    const std::string source = "function add(a, b) {\n  // sum\n  return a + b;\n}\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::JavaScript, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "function"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "return"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "comment", "// sum"));
}

TEST(SyntaxHighlightEngine, HighlightsTypeScriptOwnKeywordsOnTopOfInheritedJavaScriptKeywords)
{
    SyntaxHighlightEngine engine;
    const std::string source = "interface Point { x: number; }\nfunction id(p: Point): Point {\n  return p;\n}\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::TypeScript, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "interface")); // tree-sitter-typescript's own addition
    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "function"));  // inherited from tree-sitter-javascript
}

TEST(SyntaxHighlightEngine, HighlightsJsonStringNumberAndComment)
{
    SyntaxHighlightEngine engine;
    const std::string source = "{\n  \"a\": 1\n}\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::Json, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "string.special.key", "\"a\""));
    EXPECT_TRUE(hasSpanCovering(spans, source, "number", "1"));
}

TEST(SyntaxHighlightEngine, HighlightsHtmlTagAndComment)
{
    SyntaxHighlightEngine engine;
    const std::string source = "<!-- hi -->\n<div>text</div>\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::Html, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "tag", "div"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "comment", "<!-- hi -->"));
}

TEST(SyntaxHighlightEngine, HighlightsCssKeywordAndComment)
{
    SyntaxHighlightEngine engine;
    const std::string source = "/* hi */\n@media screen {\n  a { color: red; }\n}\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::Css, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "keyword", "@media"));
    EXPECT_TRUE(hasSpanCovering(spans, source, "comment", "/* hi */"));
}

TEST(SyntaxHighlightEngine, HighlightsNonAsciiSourceWithCorrectByteOffsets)
{
    SyntaxHighlightEngine engine;
    // "café" (Latin small letter e with acute is 2 UTF-8 bytes) inside a string literal, followed
    // by a comment -- exercises that byte offsets/lengths stay correct across multi-byte UTF-8
    // characters (Architecture.md's non-ASCII requirement, Specification.md line 34).
    const std::string source = "const char* s = \"caf\xc3\xa9\"; // note\n";

    const std::vector<HighlightSpan> spans = engine.highlight(Language::Cpp, source);

    EXPECT_TRUE(hasSpanCovering(spans, source, "comment", "// note"));
}

TEST(SyntaxHighlightEngine, CaptureNamesForAreLanguageSpecificNotAUniversalList)
{
    SyntaxHighlightEngine engine;

    const std::vector<std::string>& cppCaptures = engine.captureNamesFor(Language::Cpp);
    const std::vector<std::string>& jsonCaptures = engine.captureNamesFor(Language::Json);

    EXPECT_NE(std::find(cppCaptures.begin(), cppCaptures.end(), "keyword"), cppCaptures.end());
    // JSON's own highlights.scm defines no "keyword" capture (no language keywords in JSON) -- the
    // Settings dialog must reflect that instead of showing a hardcoded universal token list.
    EXPECT_EQ(std::find(jsonCaptures.begin(), jsonCaptures.end(), "keyword"), jsonCaptures.end());
    EXPECT_NE(std::find(jsonCaptures.begin(), jsonCaptures.end(), "number"), jsonCaptures.end());
}
