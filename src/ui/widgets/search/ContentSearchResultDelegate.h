#pragma once

#include <vector>

#include <QString>
#include <QStyledItemDelegate>

#include "ContentSearchOptions.h"

// Item delegate for "Find in Files" results (Architecture.md §14.104). Paints two distinct row
// shapes -- a file-header row (icon + display name/path-context + "(N matches)" suffix) and a
// line row ("<lineNumber>:  <text>", bold+highlighted on *every* occurrence) -- so it is not a
// reuse of SearchResultDelegate, which only ever paints one row shape and only highlights a
// line's first occurrence. Independently re-derives highlight spans at paint time from whatever
// query/TextMatchOptions are currently set, the same "don't consume spans from a matcher"
// precedent SearchResultDelegate::paintNameColumn already set -- using QString/
// QRegularExpression rather than WholeWordMatch/RegexMatch, since this is a Qt-layer painting
// concern, not a Domain one.
class ContentSearchResultDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ContentSearchResultDelegate(QObject* parent = nullptr);

    void setHighlightQuery(const QString& query, const TextMatchOptions& options);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    struct HighlightSpan
    {
        int start;
        int length;
    };

    void paintHeaderRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paintLineRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

    // Every occurrence of m_highlightQuery in `text` per m_highlightOptions.mode: Contains/Whole
    // Word via QString::indexOf looped (Whole Word additionally checks the codepoint before/after
    // via QChar::isLetterOrNumber, the Qt-side mirror of WholeWordMatch's own boundary rule);
    // Regex via QRegularExpression::globalMatch, one span per non-empty match -- an invalid
    // pattern yields no spans, same "invalid regex -> no match" posture
    // ContentSearchUseCase::textMatches already takes on the Domain side.
    std::vector<HighlightSpan> findHighlightSpans(const QString& text) const;

    QString m_highlightQuery;
    TextMatchOptions m_highlightOptions;
};
