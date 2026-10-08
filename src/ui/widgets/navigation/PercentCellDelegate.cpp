#include "PercentCellDelegate.h"

#include <QColor>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QStyle>

#include "FileListModel.h"

namespace
{
    constexpr int kBarMargin = 4;
    constexpr int kBarHeight = 16;
    const QColor kFillColor(52, 152, 219);   // same blue family as the git-status badges
    const QColor kFillTextColor(Qt::white);
}

PercentCellDelegate::PercentCellDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void PercentCellDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QVariant percentData = index.data(FileListModel::PercentRole);
    if (!percentData.isValid())
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const double percent = qBound(0.0, percentData.toDouble(), 100.0);

    painter->save();

    if (option.state & QStyle::State_Selected)
    {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    const QRect barRect(option.rect.left() + kBarMargin, option.rect.top() + (option.rect.height() - kBarHeight) / 2,
                         option.rect.width() - 2 * kBarMargin, kBarHeight);

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);

    painter->setBrush(option.palette.mid());
    painter->drawRoundedRect(barRect, barRect.height() / 2.0, barRect.height() / 2.0);

    const int fillWidth = static_cast<int>(barRect.width() * percent / 100.0);
    const QRect fillRect(barRect.left(), barRect.top(), fillWidth, barRect.height());
    if (fillWidth > 0)
    {
        painter->setBrush(kFillColor);
        painter->drawRoundedRect(fillRect, barRect.height() / 2.0, barRect.height() / 2.0);
    }

    const QString text = QStringLiteral("%1%").arg(static_cast<int>(percent + 0.5));
    painter->setFont(option.font);

    // Drawn twice, each clipped to one side of the fill boundary, so the centered label stays
    // legible regardless of where that boundary lands underneath it.
    painter->setClipRect(fillRect);
    painter->setPen(kFillTextColor);
    painter->drawText(barRect, Qt::AlignCenter, text);

    painter->setClipRect(barRect.adjusted(fillWidth, 0, 0, 0));
    painter->setPen(option.state & QStyle::State_Selected ? option.palette.highlightedText().color() : option.palette.text().color());
    painter->drawText(barRect, Qt::AlignCenter, text);

    painter->restore();
}
