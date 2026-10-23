#include "ContentSearchResultDelegate.h"

#include <QColor>
#include <QFileIconProvider>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>

#include "ContentSearchResultsModel.h"

namespace
{
    constexpr qreal kRowHeightMultiplier = 1.5;
    constexpr int kIconPadding = 4;
    constexpr int kIconSize = 16;
    constexpr int kTextPadding = 4;
}

ContentSearchResultDelegate::ContentSearchResultDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ContentSearchResultDelegate::setHighlightQuery(const QString& query, const TextMatchOptions& options)
{
    m_highlightQuery = query;
    m_highlightOptions = options;
}

std::vector<ContentSearchResultDelegate::HighlightSpan> ContentSearchResultDelegate::findHighlightSpans(const QString& text) const
{
    std::vector<HighlightSpan> spans;
    if (m_highlightQuery.isEmpty())
    {
        return spans;
    }

    const Qt::CaseSensitivity caseSensitivity = m_highlightOptions.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    switch (m_highlightOptions.mode)
    {
        case TextMatchMode::Contains:
        {
            int from = 0;
            while (true)
            {
                const int start = text.indexOf(m_highlightQuery, from, caseSensitivity);
                if (start < 0)
                {
                    break;
                }
                spans.push_back({ start, static_cast<int>(m_highlightQuery.length()) });
                from = start + m_highlightQuery.length();
            }
            break;
        }
        case TextMatchMode::WholeWord:
        {
            int from = 0;
            while (true)
            {
                const int start = text.indexOf(m_highlightQuery, from, caseSensitivity);
                if (start < 0)
                {
                    break;
                }
                const int end = start + static_cast<int>(m_highlightQuery.length());
                const bool boundaryBefore = start == 0 || !text.at(start - 1).isLetterOrNumber();
                const bool boundaryAfter = end >= text.length() || !text.at(end).isLetterOrNumber();
                if (boundaryBefore && boundaryAfter)
                {
                    spans.push_back({ start, static_cast<int>(m_highlightQuery.length()) });
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
                m_highlightOptions.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
            const QRegularExpression regex(m_highlightQuery, patternOptions);
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

void ContentSearchResultDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const bool isHeaderRow = !index.data(ContentSearchResultsModel::LineNumberRole).isValid();
    if (isHeaderRow)
    {
        paintHeaderRow(painter, option, index);
    }
    else
    {
        paintLineRow(painter, option, index);
    }
}

void ContentSearchResultDelegate::paintHeaderRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    const bool selected = option.state & QStyle::State_Selected;
    if (selected)
    {
        painter->fillRect(option.rect, option.palette.highlight());
        painter->setPen(option.palette.highlightedText().color());
    }
    else
    {
        QColor color = option.palette.text().color();
        if (index.data(ContentSearchResultsModel::IsHiddenRole).toBool())
        {
            // Equivalent to FileDecorationRules::hiddenStyle(false)'s graying intent, without a
            // dependency on FileDecorationRules here -- a plain dimmed text color is the
            // standard Qt "grayed out" treatment.
            color.setAlpha(140);
        }
        painter->setPen(color);
    }

    static QFileIconProvider iconProvider;
    const QIcon icon = iconProvider.icon(QFileIconProvider::File);
    const QRect iconRect(option.rect.left() + kIconPadding, option.rect.top() + (option.rect.height() - kIconSize) / 2, kIconSize,
                         kIconSize);
    icon.paint(painter, iconRect);

    const QRect textRect(iconRect.right() + kIconPadding, option.rect.top(), option.rect.right() - iconRect.right() - 2 * kIconPadding,
                          option.rect.height());

    const int matchCount = index.model()->rowCount(index);
    const QString suffix = matchCount == 1 ? tr(" (1 match)") : tr(" (%1 matches)").arg(matchCount);
    const QString text = index.data(Qt::DisplayRole).toString() + suffix;

    const QFontMetrics metrics(option.font);
    painter->setFont(option.font);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(text, Qt::ElideRight, textRect.width()));

    painter->restore();
}

void ContentSearchResultDelegate::paintLineRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    const bool selected = option.state & QStyle::State_Selected;
    if (selected)
    {
        painter->fillRect(option.rect, option.palette.highlight());
        painter->setPen(option.palette.highlightedText().color());
    }
    else
    {
        painter->setPen(option.palette.text().color());
    }

    const int lineNumber = index.data(ContentSearchResultsModel::LineNumberRole).toInt();
    const QString lineText = index.data(ContentSearchResultsModel::FullLineTextRole).toString();
    const QString prefix = QStringLiteral("%1:  ").arg(lineNumber);

    const QFont font = option.font;
    const QFontMetrics metrics(font);
    QFont boldFont = font;
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);

    const QRect textRect(option.rect.left() + kTextPadding, option.rect.top(), option.rect.width() - 2 * kTextPadding,
                          option.rect.height());
    const int y = textRect.top();
    const int height = textRect.height();
    int x = textRect.left();

    painter->setFont(font);
    const int prefixAvailable = textRect.right() - x + 1;
    const QString elidedPrefix = metrics.elidedText(prefix, Qt::ElideRight, prefixAvailable);
    painter->drawText(QRect(x, y, prefixAvailable, height), Qt::AlignLeft | Qt::AlignVCenter, elidedPrefix);
    if (elidedPrefix != prefix)
    {
        painter->restore();
        return;
    }
    x += metrics.horizontalAdvance(prefix);

    const std::vector<HighlightSpan> spans = findHighlightSpans(lineText);
    int cursor = 0;

    for (const HighlightSpan& span : spans)
    {
        if (x > textRect.right())
        {
            painter->restore();
            return;
        }

        if (span.start > cursor)
        {
            const QString segment = lineText.mid(cursor, span.start - cursor);
            const int available = textRect.right() - x + 1;
            if (available <= 0)
            {
                painter->restore();
                return;
            }
            painter->setFont(font);
            const QString elidedSegment = metrics.elidedText(segment, Qt::ElideRight, available);
            painter->drawText(QRect(x, y, available, height), Qt::AlignLeft | Qt::AlignVCenter, elidedSegment);
            if (elidedSegment != segment)
            {
                painter->restore();
                return;
            }
            x += metrics.horizontalAdvance(segment);
        }

        const QString match = lineText.mid(span.start, span.length);
        const int available = textRect.right() - x + 1;
        if (available <= 0)
        {
            painter->restore();
            return;
        }
        const QString elidedMatch = boldMetrics.elidedText(match, Qt::ElideRight, available);
        if (!selected)
        {
            painter->fillRect(QRect(x, y, boldMetrics.horizontalAdvance(elidedMatch), height), option.palette.alternateBase());
        }
        painter->setFont(boldFont);
        painter->drawText(QRect(x, y, available, height), Qt::AlignLeft | Qt::AlignVCenter, elidedMatch);
        if (elidedMatch != match)
        {
            painter->restore();
            return;
        }
        x += boldMetrics.horizontalAdvance(match);
        cursor = span.start + span.length;
    }

    if (cursor < lineText.length() && x <= textRect.right())
    {
        const QString segment = lineText.mid(cursor);
        const int available = textRect.right() - x + 1;
        if (available > 0)
        {
            painter->setFont(font);
            painter->drawText(QRect(x, y, available, height), Qt::AlignLeft | Qt::AlignVCenter,
                               metrics.elidedText(segment, Qt::ElideRight, available));
        }
    }

    painter->restore();
}

QSize ContentSearchResultDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    hint.setHeight(static_cast<int>(hint.height() * kRowHeightMultiplier));
    return hint;
}
