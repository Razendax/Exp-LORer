#include "FileTileDelegate.h"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLocale>
#include <QPainter>
#include <QWidget>

#include "FileListModel.h"

namespace
{
    constexpr int kIconSize = 48;
    constexpr int kTileWidth = 220;
    constexpr int kTileHeight = 56;
    constexpr int kPadding = 8;

    // The row's Qt::FontRole (set by a decoration rule) if it has one, otherwise the view's
    // default font -- paint(), nameRectFor() and sizeHint() must all agree on this or a
    // rule-supplied larger font gets clipped by a name rect/tile height sized for the default font.
    QFont resolvedFont(const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        const QVariant fontData = index.data(Qt::FontRole);
        return fontData.canConvert<QFont>() ? fontData.value<QFont>() : option.font;
    }

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
    : FileNameEditDelegate(parent)
{
}

QRect FileTileDelegate::nameRectFor(const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const QRect iconRect(option.rect.left() + kPadding,
                          option.rect.top() + (option.rect.height() - kIconSize) / 2,
                          kIconSize,
                          kIconSize);

    const QRect textRect(iconRect.right() + kPadding,
                          option.rect.top() + kPadding,
                          option.rect.right() - iconRect.right() - 2 * kPadding,
                          option.rect.height() - 2 * kPadding);

    const QFontMetrics nameMetrics(resolvedFont(option, index));
    return QRect(textRect.left(), textRect.top(), textRect.width(), nameMetrics.height());
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
        const QVariant foreground = index.data(Qt::ForegroundRole);
        painter->setPen(foreground.canConvert<QColor>() ? foreground.value<QColor>() : option.palette.text().color());
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

    const QFont font = resolvedFont(option, index);

    const QFontMetrics nameMetrics(font);
    const QRect nameRect = nameRectFor(option, index);
    const QRect secondaryRect(textRect.left(), nameRect.bottom(), textRect.width(), nameMetrics.height());

    const QString name = index.data(Qt::DisplayRole).toString();
    painter->setFont(font);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(name, Qt::ElideRight, nameRect.width()));

    QFont secondaryFont = font;
    secondaryFont.setPointSizeF(secondaryFont.pointSizeF() * 0.85);
    const QFontMetrics secondaryMetrics(secondaryFont);
    painter->setFont(secondaryFont);
    const QString secondary = secondaryLine(index);
    painter->drawText(secondaryRect, Qt::AlignLeft | Qt::AlignVCenter, secondaryMetrics.elidedText(secondary, Qt::ElideRight, secondaryRect.width()));

    painter->restore();
}

QSize FileTileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QFont font = resolvedFont(option, index);
    const QFontMetrics nameMetrics(font);

    QFont secondaryFont = font;
    secondaryFont.setPointSizeF(secondaryFont.pointSizeF() * 0.85);
    const QFontMetrics secondaryMetrics(secondaryFont);

    const int contentHeight = 2 * kPadding + nameMetrics.height() + secondaryMetrics.height();
    return QSize(kTileWidth, std::max(kTileHeight, contentHeight));
}

void FileTileDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    editor->setGeometry(nameRectFor(option, index));
}
