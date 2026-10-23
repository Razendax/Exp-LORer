#include "ContentSearchTextMatching.h"

#include <QRegularExpression>

std::vector<MatchSpan> findMatches(const QString& text, const QString& query, const TextMatchOptions& options)
{
    std::vector<MatchSpan> spans;
    if (query.isEmpty())
    {
        return spans;
    }

    const Qt::CaseSensitivity caseSensitivity = options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    switch (options.mode)
    {
        case TextMatchMode::Contains:
        {
            int from = 0;
            while (true)
            {
                const int start = text.indexOf(query, from, caseSensitivity);
                if (start < 0)
                {
                    break;
                }
                spans.push_back({ start, static_cast<int>(query.length()) });
                from = start + query.length();
            }
            break;
        }
        case TextMatchMode::WholeWord:
        {
            int from = 0;
            while (true)
            {
                const int start = text.indexOf(query, from, caseSensitivity);
                if (start < 0)
                {
                    break;
                }
                const int end = start + static_cast<int>(query.length());
                const bool boundaryBefore = start == 0 || !text.at(start - 1).isLetterOrNumber();
                const bool boundaryAfter = end >= text.length() || !text.at(end).isLetterOrNumber();
                if (boundaryBefore && boundaryAfter)
                {
                    spans.push_back({ start, static_cast<int>(query.length()) });
                    from = end;
                }
                else
                {
                    from = start + 1;
                }
            }
            break;
        }
        case TextMatchMode::Regex:
        {
            const QRegularExpression::PatternOptions patternOptions =
                options.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
            const QRegularExpression regex(query, patternOptions);
            if (!regex.isValid())
            {
                break;
            }
            QRegularExpressionMatchIterator it = regex.globalMatch(text);
            while (it.hasNext())
            {
                const QRegularExpressionMatch match = it.next();
                if (match.capturedLength() == 0)
                {
                    // Avoid an infinite/degenerate span from a pattern that can match empty
                    // (e.g. "a*").
                    continue;
                }
                spans.push_back({ static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()) });
            }
            break;
        }
    }

    return spans;
}
