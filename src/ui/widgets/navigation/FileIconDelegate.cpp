#include "FileIconDelegate.h"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QWidget>

namespace
{
    constexpr int kPadding = 8;
    constexpr int kMinTextWidth = 60;

    // The row's Qt::FontRole (set by a decoration rule) if it has one, otherwise the view's
    // default font -- paint(), nameRectFor() and sizeHint() must all agree on this or a
    // rule-supplied larger font gets clipped by a text rect/row height sized for the default font.
    QFont resolvedFont(const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        const QVariant fontData = index.data(Qt::FontRole);
        return fontData.canConvert<QFont>() ? fontData.value<QFont>() : option.font;
    }

    // QIcon::paint() never upscales: its engine picks the closest available pixmap and only
    // shrinks it to fit, never grows it, so a row icon resolved from a single native-resolution
    // Shell pixmap (IconLoadingWorker's SHGFI_LARGEICON, ~32x32) renders tiny and centered inside
    // a Medium/Large/Extra Large grid cell instead of filling it. Explorer itself stretches that
    // same native pixmap to fill the cell in those modes, so draw the largest available pixmap
    // straight into `rect` -- QPainter::drawPixmap(QRect, QPixmap) always scales to fill it.
    void paintIcon(QPainter* painter, const QIcon& icon, const QRect& rect)
    {
        if (icon.isNull())
        {
            return;
        }

        const QList<QSize> sizes = icon.availableSizes();
        QSize bestSize = rect.size();
        for (const QSize& size : sizes)
        {
            if (size.width() * size.height() > bestSize.width() * bestSize.height())
            {
                bestSize = size;
            }
        }

        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawPixmap(rect, icon.pixmap(bestSize));
    }
}

FileIconDelegate::FileIconDelegate(QObject* parent)
    : FileNameEditDelegate(parent)
{
}

QRect FileIconDelegate::nameRectFor(const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const QSize iconSize = option.decorationSize;
    const QRect iconRect(option.rect.left() + (option.rect.width() - iconSize.width()) / 2,
                          option.rect.top() + kPadding,
                          iconSize.width(),
                          iconSize.height());

    const QFontMetrics fontMetrics(resolvedFont(option, index));
    return QRect(option.rect.left() + kPadding,
                 iconRect.bottom() + kPadding,
                 option.rect.width() - 2 * kPadding,
                 fontMetrics.height());
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
    paintIcon(painter, icon, iconRect);

    const QRect textRect = nameRectFor(option, index);

    const QFont font = resolvedFont(option, index);
    const QFontMetrics fontMetrics(font);

    painter->setFont(font);
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
    const QSize iconSize = option.decorationSize;
    const QFontMetrics fontMetrics(resolvedFont(option, index));

    const int width = std::max(iconSize.width(), kMinTextWidth) + 2 * kPadding;
    const int height = iconSize.height() + kPadding + fontMetrics.height() + kPadding;

    return QSize(width, height);
}

void FileIconDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    editor->setGeometry(nameRectFor(option, index));
}
