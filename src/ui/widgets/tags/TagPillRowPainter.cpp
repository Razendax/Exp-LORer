#include "TagPillRowPainter.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QSize>
#include <QString>

#include "Tag.h"
#include "TagChipColor.h"

namespace
{
    constexpr int kPillSpacing = 4;
    constexpr int kPillPaddingH = 8;
    constexpr int kPillPaddingV = 2;

    QSize pillSizeFor(const QFontMetrics& fontMetrics, const QString& text)
    {
        return QSize(fontMetrics.horizontalAdvance(text) + 2 * kPillPaddingH, fontMetrics.height() + 2 * kPillPaddingV);
    }

    void paintPill(QPainter* painter, const QRect& rect, const QColor& background, const QString& text, const QFont& font)
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);

        painter->setFont(font);
        painter->setPen(QColor(tagChipTextColorFor(background)));
        painter->drawText(rect, Qt::AlignCenter, text);
        painter->restore();
    }
}

void TagPillRowPainter::paintTagPillRow(QPainter* painter, const QRect& rect, const std::vector<Tag>& tags, const QFont& font)
{
    const QFontMetrics fontMetrics(font);
    const int rowHeight = tagPillRowHeight(fontMetrics);
    const int top = rect.top() + (rect.height() - rowHeight) / 2;

    int x = rect.left();
    const int rightLimit = rect.right();

    std::size_t shown = 0;
    for (; shown < tags.size(); ++shown)
    {
        const QString name = QString::fromStdString(tags[shown].name());
        const QSize size = pillSizeFor(fontMetrics, name);

        // Reserve room for a trailing "+N" pill if tags remain after this one, so that pill never
        // gets clipped off the right edge by an optimistic fit of the current one.
        const bool moreRemain = shown + 1 < tags.size();
        const int reserve = moreRemain
            ? pillSizeFor(fontMetrics, QStringLiteral("+%1").arg(tags.size() - shown - 1)).width() + kPillSpacing
            : 0;

        if (shown > 0 && x + size.width() > rightLimit - reserve)
        {
            break;
        }

        paintPill(painter, QRect(x, top, size.width(), size.height()), QColor(QString::fromStdString(tags[shown].hexColor())), name,
                  font);
        x += size.width() + kPillSpacing;
    }

    if (shown < tags.size())
    {
        const QString overflow = QStringLiteral("+%1").arg(tags.size() - shown);
        const QSize size = pillSizeFor(fontMetrics, overflow);
        paintPill(painter, QRect(x, top, size.width(), size.height()), QGuiApplication::palette().mid().color(), overflow, font);
    }
}

int TagPillRowPainter::tagPillRowHeight(const QFontMetrics& fontMetrics)
{
    return fontMetrics.height() + 2 * kPillPaddingV;
}
