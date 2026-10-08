#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QSyntaxHighlighter>

#include "Language.h"

class HighlightThemeViewModel;
class SyntaxHighlightEngine;
class QTextDocument;

// Attached to PreviewPanelWidget's text view QTextDocument (Architecture.md §14.26). Preview text
// is capped and read-only -- never live-edited -- so spans are computed once per setLanguage() call
// (i.e. once per preview load) rather than re-running the parser/query from highlightBlock() on
// every block, the same "one-shot cost per preview load, not per keystroke" posture the feature was
// designed around.
class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    SyntaxHighlighter(QTextDocument* document, SyntaxHighlightEngine& engine, HighlightThemeViewModel& themeViewModel);

    // std::nullopt leaves the document unhighlighted (an extension LanguageRegistry doesn't map,
    // e.g. .txt/.md) -- same as no highlighter being attached at all.
    void setLanguage(std::optional<Language> language);

protected:
    void highlightBlock(const QString& text) override;

private:
    // Whole-document span, in UTF-16 QString character offsets (tree-sitter reports UTF-8 byte
    // offsets into the source it was given -- converted once here so highlightBlock() never has to
    // reason about the byte/UTF-16 gap non-ASCII text opens up, Architecture.md's non-ASCII
    // requirement, Specification.md line 34).
    struct DocumentSpan
    {
        int start = 0;
        int length = 0;
        std::string captureName;
    };

    void recomputeSpans();

    SyntaxHighlightEngine& m_engine;
    HighlightThemeViewModel& m_themeViewModel;
    std::optional<Language> m_language;
    std::vector<DocumentSpan> m_spans;

    // m_spans bucketed by block number so highlightBlock() only scans the spans that actually
    // overlap its own block, instead of the whole document's span list on every block (an
    // O(numBlocks * numSpans) scan otherwise). Points into m_spans, which recomputeSpans() rebuilds
    // in full (no incremental push_back) before this is populated, so the pointers stay valid for
    // as long as this vector does.
    std::vector<std::vector<const DocumentSpan*>> m_spansByBlock;
};
