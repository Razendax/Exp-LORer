#include "FileTileDelegate.h"

#include <QFontMetrics>
#include <QLocale>
#include <QPainter>

#include "FileListModel.h"

namespace
{
    constexpr int kIconSize = 48;
    constexpr int kTileWidth = 220;
    constexpr int kTileHeight = 56;
    constexpr int kPadding = 8;

    QString secondaryLine(const QModelIndex& index)
    {
        const QModelIndex typeIndex = index.sibling(index.row(), FileListModel::TypeColumn);
        const QModelIndex sizeIndex = index.sibling(index.row(), FileListModel::SizeColumn);

        const QString type = typeIndex.data(Qt::DisplayRole).toString();
        const QVariant sizeValue = sizeIndex.data(Qt::DisplayRole);

        if (!sizeValue.isValid())
        {
            return type;
        }

        return type + QStringLiteral("   ") + QLocale().formattedDataSize(sizeValue.toULongLong());
    }
}

FileTileDelegate::FileTileDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void FileTileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    if (option.state & QStyle::State_Selected)
    {
        painter->fillRect(option.rect, option.palette.highlight());
        painter->setPen(option.palette.highlightedText().color());
    }
    else
    {
        painter->setPen(option.palette.text().color());
    }

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QRect iconRect(option.rect.left() + kPadding,
                          option.rect.top() + (option.rect.height() - kIconSize) / 2,
                          kIconSize,
                          kIconSize);
    icon.paint(painter, iconRect);

    const QRect textRect(iconRect.right() + kPadding,
                          option.rect.top() + kPadding,
                          option.rect.right() - iconRect.right() - 2 * kPadding,
                          option.rect.height() - 2 * kPadding);

    const QFontMetrics nameMetrics(option.font);
    const QRect nameRect(textRect.left(), textRect.top(), textRect.width(), nameMetrics.height());
    const QRect secondaryRect(textRect.left(), nameRect.bottom(), textRect.width(), nameMetrics.height());

    const QString name = index.data(Qt::DisplayRole).toString();
    painter->setFont(option.font);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(name, Qt::ElideRight, nameRect.width()));

    QFont secondaryFont = option.font;
    secondaryFont.setPointSizeF(secondaryFont.pointSizeF() * 0.85);
    const QFontMetrics secondaryMetrics(secondaryFont);
    painter->setFont(secondaryFont);
    const QString secondary = secondaryLine(index);
    painter->drawText(secondaryRect, Qt::AlignLeft | Qt::AlignVCenter, secondaryMetrics.elidedText(secondary, Qt::ElideRight, secondaryRect.width()));

    painter->restore();
}

QSize FileTileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);

    return QSize(kTileWidth, kTileHeight);
}
