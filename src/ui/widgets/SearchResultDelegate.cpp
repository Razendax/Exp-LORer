#include "SearchResultDelegate.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>

#include "FileListModel.h"

namespace
{
    constexpr qreal kRowHeightMultiplier = 1.5;
    constexpr int kIconPadding = 4;
    constexpr int kIconSize = 16;
}

SearchResultDelegate::SearchResultDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void SearchResultDelegate::setHighlightQuery(const QString& query)
{
    m_highlightQuery = query;
}

void SearchResultDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (index.column() != FileListModel::NameColumn)
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    paintNameColumn(painter, option, index);
}

void SearchResultDelegate::paintNameColumn(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
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
        const QVariant foreground = index.data(Qt::ForegroundRole);
        painter->setPen(foreground.canConvert<QColor>() ? foreground.value<QColor>() : option.palette.text().color());
    }

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QRect iconRect(option.rect.left() + kIconPadding,
                          option.rect.top() + (option.rect.height() - kIconSize) / 2,
                          kIconSize,
                          kIconSize);
    icon.paint(painter, iconRect);

    const QRect textRect(iconRect.right() + kIconPadding,
                          option.rect.top(),
                          option.rect.right() - iconRect.right() - 2 * kIconPadding,
                          option.rect.height());

    const QString name = index.data(Qt::DisplayRole).toString();

    QFont font = option.font;
    const QVariant fontData = index.data(Qt::FontRole);
    if (fontData.canConvert<QFont>())
    {
        font = fontData.value<QFont>();
    }

    painter->setFont(font);
    const QFontMetrics metrics(font);

    int matchStart = -1;
    int matchLength = 0;
    if (!m_highlightQuery.isEmpty())
    {
        matchStart = name.indexOf(m_highlightQuery, 0, Qt::CaseInsensitive);
        matchLength = m_highlightQuery.length();
    }

    if (matchStart < 0)
    {
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(name, Qt::ElideRight, textRect.width()));
        painter->restore();
        return;
    }

    const QString before = name.left(matchStart);
    const QString match = name.mid(matchStart, matchLength);
    const QString after = name.mid(matchStart + matchLength);

    int x = textRect.left();
    const int y = textRect.top();
    const int height = textRect.height();
    const int available = textRect.width();

    if (!before.isEmpty())
    {
        const QString elidedBefore = metrics.elidedText(before, Qt::ElideRight, available);
        painter->drawText(QRect(x, y, available, height), Qt::AlignLeft | Qt::AlignVCenter, elidedBefore);
        if (elidedBefore != before)
        {
            // Ran out of room already eliding the prefix; the match/suffix have no space left.
            painter->restore();
            return;
        }
        x += metrics.horizontalAdvance(before);
    }

    const int remainingAfterBefore = textRect.right() - x + 1;
    if (remainingAfterBefore <= 0)
    {
        painter->restore();
        return;
    }

    // Layers bold on top of the row's own resolved font (rather than replacing it) so a
    // customized row's font/style survives inside the highlighted match span too.
    QFont boldFont = font;
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);
    const QString elidedMatch = boldMetrics.elidedText(match, Qt::ElideRight, remainingAfterBefore);

    if (!selected)
    {
        const QRect matchBackgroundRect(x, y, boldMetrics.horizontalAdvance(elidedMatch), height);
        painter->fillRect(matchBackgroundRect, option.palette.alternateBase());
    }

    painter->setFont(boldFont);
    painter->drawText(QRect(x, y, remainingAfterBefore, height), Qt::AlignLeft | Qt::AlignVCenter, elidedMatch);

    if (elidedMatch == match)
    {
        x += boldMetrics.horizontalAdvance(match);
        const int remainingAfterMatch = textRect.right() - x + 1;
        if (remainingAfterMatch > 0 && !after.isEmpty())
        {
            painter->setFont(font);
            painter->drawText(QRect(x, y, remainingAfterMatch, height), Qt::AlignLeft | Qt::AlignVCenter,
                               metrics.elidedText(after, Qt::ElideRight, remainingAfterMatch));
        }
    }

    painter->restore();
}

QSize SearchResultDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    hint.setHeight(static_cast<int>(hint.height() * kRowHeightMultiplier));
    return hint;
}
