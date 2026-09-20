#include "SyntaxHighlighter.h"

#include <algorithm>

#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>

#include "HighlightThemeViewModel.h"
#include "SyntaxHighlightEngine.h"

namespace
{
    // Builds a byte-offset -> UTF-16-offset table for `text` encoded as UTF-8, so a tree-sitter
    // HighlightSpan's byte offsets (always on UTF-8 character boundaries -- tree-sitter never
    // splits a multi-byte sequence) can be converted to the QString character offsets
    // QSyntaxHighlighter::setFormat expects. table[byteOffset] is undefined for a byteOffset that
    // isn't itself a character boundary, which never happens for a span tree-sitter produced.
    std::vector<int> buildByteToUtf16Table(const QString& text, const QByteArray& utf8)
    {
        std::vector<int> table(static_cast<std::size_t>(utf8.size()) + 1, 0);

        int utf16Index = 0;
        int byteIndex = 0;
        int i = 0;
        while (i < text.size())
        {
            const QChar ch = text.at(i);
            char32_t codepoint;
            int utf16Units;
            if (ch.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
            {
                codepoint = QChar::surrogateToUcs4(ch, text.at(i + 1));
                utf16Units = 2;
            }
            else
            {
                codepoint = ch.unicode();
                utf16Units = 1;
            }

            int utf8Units;
            if (codepoint <= 0x7F)
                utf8Units = 1;
            else if (codepoint <= 0x7FF)
                utf8Units = 2;
            else if (codepoint <= 0xFFFF)
                utf8Units = 3;
            else
                utf8Units = 4;

            for (int b = 0; b < utf8Units && byteIndex < static_cast<int>(table.size()); ++b)
            {
                table[static_cast<std::size_t>(byteIndex)] = utf16Index;
                ++byteIndex;
            }

            utf16Index += utf16Units;
            i += utf16Units;
        }

        if (byteIndex < static_cast<int>(table.size()))
        {
            table[static_cast<std::size_t>(byteIndex)] = utf16Index;
        }
        return table;
    }
}

SyntaxHighlighter::SyntaxHighlighter(QTextDocument* document, SyntaxHighlightEngine& engine, HighlightThemeViewModel& themeViewModel)
    : QSyntaxHighlighter(document)
    , m_engine(engine)
    , m_themeViewModel(themeViewModel)
{
    connect(&m_themeViewModel, &HighlightThemeViewModel::themeChanged, this, [this]() { rehighlight(); });
}

void SyntaxHighlighter::setLanguage(std::optional<Language> language)
{
    m_language = language;
    recomputeSpans();
    rehighlight();
}

void SyntaxHighlighter::recomputeSpans()
{
    m_spans.clear();
    m_spansByBlock.clear();

    if (!m_language || !document())
    {
        return;
    }

    const QString fullText = document()->toPlainText();
    const QByteArray utf8 = fullText.toUtf8();
    const std::string source(utf8.constData(), static_cast<std::size_t>(utf8.size()));

    const std::vector<HighlightSpan> byteSpans = m_engine.highlight(*m_language, source);
    if (byteSpans.empty())
    {
        return;
    }

    const std::vector<int> byteToUtf16 = buildByteToUtf16Table(fullText, utf8);

    m_spans.reserve(byteSpans.size());
    for (const HighlightSpan& span : byteSpans)
    {
        const std::size_t endByte = std::min(span.byteOffset + span.byteLength, byteToUtf16.size() - 1);
        const std::size_t startByte = std::min(span.byteOffset, endByte);

        const int start = byteToUtf16[startByte];
        const int end = byteToUtf16[endByte];
        if (end <= start)
        {
            continue;
        }

        m_spans.push_back({ start, end - start, span.captureName });
    }

    // Bucket every span into each block it overlaps (usually just one; a multi-line construct like
    // a block comment touches a few) so highlightBlock() below only ever scans its own block's
    // spans. m_spans is fully built above with no further insertions after this point, so these
    // pointers stay valid for as long as m_spans does.
    m_spansByBlock.assign(static_cast<std::size_t>(document()->blockCount()), {});
    for (const DocumentSpan& span : m_spans)
    {
        const int spanEnd = span.start + span.length;
        for (QTextBlock block = document()->findBlock(span.start); block.isValid() && block.position() < spanEnd;
             block = block.next())
        {
            const auto blockNumber = static_cast<std::size_t>(block.blockNumber());
            if (blockNumber < m_spansByBlock.size())
            {
                m_spansByBlock[blockNumber].push_back(&span);
            }
        }
    }
}

void SyntaxHighlighter::highlightBlock(const QString& text)
{
    if (!m_language)
    {
        return;
    }

    const auto blockNumber = static_cast<std::size_t>(currentBlock().blockNumber());
    if (blockNumber >= m_spansByBlock.size())
    {
        return;
    }

    const int blockStart = currentBlock().position();
    const int blockEnd = blockStart + text.length();

    for (const DocumentSpan* span : m_spansByBlock[blockNumber])
    {
        const int spanStart = span->start;
        const int spanEnd = span->start + span->length;
        if (spanEnd <= blockStart || spanStart >= blockEnd)
        {
            continue;
        }

        const QColor color = m_themeViewModel.tokenColor(*m_language, span->captureName);
        if (!color.isValid())
        {
            continue;
        }

        const int overlapStart = std::max(spanStart, blockStart);
        const int overlapEnd = std::min(spanEnd, blockEnd);

        QTextCharFormat format;
        format.setForeground(color);
        setFormat(overlapStart - blockStart, overlapEnd - overlapStart, format);
    }
}
