#pragma once

#include <QString>
#include <QStyledItemDelegate>

// Item delegate for the advanced (criteria) search pane's results QTreeView
// (Architecture.md §14.19). Default painting for every column except Name; for Name, the
// case-insensitive substring matching setHighlightQuery() is drawn bold with a subtle highlight
// background. Rows are taller than the stock Details view to satisfy "a bit larger."
class SearchResultDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SearchResultDelegate(QObject* parent = nullptr);

    // caseSensitive (Architecture.md §14.44) governs the Qt::CaseSensitive/Qt::CaseInsensitive flag
    // used to locate the highlighted span -- default false preserves every existing call site.
    void setHighlightQuery(const QString& query, bool caseSensitive = false);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    void paintNameColumn(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

    QString m_highlightQuery;
    bool m_highlightCaseSensitive = false;
};
