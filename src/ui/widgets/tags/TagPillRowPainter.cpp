#include "TagPillRowPainter.h"

#include <optional>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPoint>
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

    // One laid-out pill: its rect, display text, background, and the index into `tags` it
    // represents (std::nullopt for the trailing "+N" overflow pill, which isn't associated with
    // any single tag). Shared by paintTagPillRow and tagIdAt so painting and hit-testing never
    // drift apart (Architecture.md §14.72).
    struct PillLayout
    {
        QRect rect;
        QString text;
        QColor background;
        std::optional<std::size_t> tagIndex;
    };

    std::vector<PillLayout> layoutTagPillRow(const QRect& rect, const std::vector<Tag>& tags, const QFontMetrics& fontMetrics)
    {
        const int rowHeight = TagPillRowPainter::tagPillRowHeight(fontMetrics);
        const int top = rect.top() + (rect.height() - rowHeight) / 2;

        int x = rect.left();
        const int rightLimit = rect.right();

        std::vector<PillLayout> layout;

        std::size_t shown = 0;
        for (; shown < tags.size(); ++shown)
        {
            const QString name = QString::fromStdString(tags[shown].name());
            const QSize size = pillSizeFor(fontMetrics, name);

            // Reserve room for a trailing "+N" pill if tags remain after this one, so that pill
            // never gets clipped off the right edge by an optimistic fit of the current one.
            const bool moreRemain = shown + 1 < tags.size();
            const int reserve = moreRemain
                ? pillSizeFor(fontMetrics, QStringLiteral("+%1").arg(tags.size() - shown - 1)).width() + kPillSpacing
                : 0;

            if (shown > 0 && x + size.width() > rightLimit - reserve)
            {
                break;
            }

            layout.push_back(PillLayout{ QRect(x, top, size.width(), size.height()), name,
                                          QColor(QString::fromStdString(tags[shown].hexColor())), shown });
            x += size.width() + kPillSpacing;
        }

        if (shown < tags.size())
        {
            const QString overflow = QStringLiteral("+%1").arg(tags.size() - shown);
            const QSize size = pillSizeFor(fontMetrics, overflow);
            layout.push_back(PillLayout{ QRect(x, top, size.width(), size.height()), overflow,
                                          QGuiApplication::palette().mid().color(), std::nullopt });
        }

        return layout;
    }
}

void TagPillRowPainter::paintTagPillRow(QPainter* painter, const QRect& rect, const std::vector<Tag>& tags, const QFont& font)
{
    const QFontMetrics fontMetrics(font);
    for (const PillLayout& pill : layoutTagPillRow(rect, tags, fontMetrics))
    {
        paintPill(painter, pill.rect, pill.background, pill.text, font);
    }
}

int TagPillRowPainter::tagPillRowHeight(const QFontMetrics& fontMetrics)
{
    return fontMetrics.height() + 2 * kPillPaddingV;
}

std::optional<std::int64_t> TagPillRowPainter::tagIdAt(const QRect& rect, const std::vector<Tag>& tags, const QFont& font,
                                                         const QPoint& pos)
{
    const QFontMetrics fontMetrics(font);
    for (const PillLayout& pill : layoutTagPillRow(rect, tags, fontMetrics))
    {
        if (pill.tagIndex && pill.rect.contains(pos))
        {
            return tags[*pill.tagIndex].id();
        }
    }
    return std::nullopt;
}
