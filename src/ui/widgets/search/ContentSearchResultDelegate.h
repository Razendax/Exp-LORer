#pragma once

#include <vector>

#include <QString>
#include <QStyledItemDelegate>

#include "ContentSearchOptions.h"
#include "ContentSearchTextMatching.h"

// Item delegate for "Find in Files" results (Architecture.md §14.104). Paints two distinct row
// shapes -- a file-header row (icon + display name/path-context + "(N matches)" suffix) and a
// line row ("<lineNumber>:  <text>", bold+highlighted on *every* occurrence) -- so it is not a
// reuse of SearchResultDelegate, which only ever paints one row shape and only highlights a
// line's first occurrence. Independently re-derives highlight spans at paint time from whatever
// query/TextMatchOptions are currently set, the same "don't consume spans from a matcher"
// precedent SearchResultDelegate::paintNameColumn already set -- via the shared findMatches()
// (ContentSearchTextMatching.h), also used by Preview's own content-search highlight layer
// (Architecture.md §14.105), so the two are provably in agreement.
class ContentSearchResultDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ContentSearchResultDelegate(QObject* parent = nullptr);

    void setHighlightQuery(const QString& query, const TextMatchOptions& options);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    void paintHeaderRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paintLineRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

    QString m_highlightQuery;
    TextMatchOptions m_highlightOptions;
};
