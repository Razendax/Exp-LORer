#include "FileIconDelegate.h"

#include <algorithm>

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>

namespace
{
    constexpr int kPadding = 8;
    constexpr int kMinTextWidth = 60;
}

FileIconDelegate::FileIconDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void FileIconDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QSize iconSize = option.decorationSize;
    const QRect iconRect(option.rect.left() + (option.rect.width() - iconSize.width()) / 2,
                          option.rect.top() + kPadding,
                          iconSize.width(),
                          iconSize.height());
    icon.paint(painter, iconRect);

    const QFontMetrics fontMetrics(option.font);
    const QRect textRect(option.rect.left() + kPadding,
                          iconRect.bottom() + kPadding,
                          option.rect.width() - 2 * kPadding,
                          fontMetrics.height());

    painter->setFont(option.font);
    const QVariant foreground = index.data(Qt::ForegroundRole);
    if (!(option.state & QStyle::State_Selected) && foreground.canConvert<QColor>())
    {
        painter->setPen(foreground.value<QColor>());
    }
    else
    {
        painter->setPen(option.palette.text().color());
    }
    const QString name = index.data(Qt::DisplayRole).toString();
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter,
                       fontMetrics.elidedText(name, Qt::ElideRight, textRect.width()));

    if (option.state & QStyle::State_Selected)
    {
        QPen pen(Qt::blue, 1, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
    }

    painter->restore();
}

QSize FileIconDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);

    const QSize iconSize = option.decorationSize;
    const QFontMetrics fontMetrics(option.font);

    const int width = std::max(iconSize.width(), kMinTextWidth) + 2 * kPadding;
    const int height = iconSize.height() + kPadding + fontMetrics.height() + kPadding;

    return QSize(width, height);
}
