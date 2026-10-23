#pragma once

#include <vector>

#include <QString>

#include "ContentSearchOptions.h"

// Shared span-finder behind both ContentSearchResultDelegate's results-list highlighting and
// Preview's content-search highlight layer (Architecture.md §14.104/§14.105) -- a single code
// path so the two are provably in agreement, not just "built against the same options struct".
// Stays Qt-based (QString/QRegularExpression) rather than Domain, same rationale
// ContentSearchResultDelegate's own former findHighlightSpans already documented: this is a
// UI-layer painting/highlighting concern, not a Domain one.
struct MatchSpan
{
    int start;
    int length;
};

// Every occurrence of `query` in `text` per `options.mode`: Contains/Whole Word via
// QString::indexOf looped (Whole Word additionally checks the codepoint before/after via
// QChar::isLetterOrNumber, the Qt-side mirror of WholeWordMatch's own boundary rule); Regex via
// QRegularExpression::globalMatch, one span per non-empty match -- an invalid pattern yields no
// spans, same "invalid regex -> no match" posture ContentSearchUseCase::textMatches already takes
// on the Domain side. Empty `query` yields no spans.
std::vector<MatchSpan> findMatches(const QString& text, const QString& query, const TextMatchOptions& options);
