#include "TagsCellDelegate.h"

#include <vector>

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QString>

#include "FileListModel.h"
#include "Tag.h"
#include "TagChipColor.h"

namespace
{
    constexpr int kPillSpacing = 4;
    constexpr int kPillPaddingH = 8;
    constexpr int kPillPaddingV = 2;
    constexpr int kRowMargin = 4;

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

TagsCellDelegate::TagsCellDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void TagsCellDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto tags = index.data(FileListModel::TagsRole).value<std::vector<Tag>>();
    if (tags.empty())
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    painter->save();
    if (option.state & QStyle::State_Selected)
    {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    const QFontMetrics fontMetrics(option.font);
    const int rowHeight = fontMetrics.height() + 2 * kPillPaddingV;
    const int top = option.rect.top() + (option.rect.height() - rowHeight) / 2;

    int x = option.rect.left() + kRowMargin;
    const int rightLimit = option.rect.right() - kRowMargin;

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

        paintPill(painter, QRect(x, top, size.width(), size.height()),
                  QColor(QString::fromStdString(tags[shown].hexColor())), name, option.font);
        x += size.width() + kPillSpacing;
    }

    if (shown < tags.size())
    {
        const QString overflow = QStringLiteral("+%1").arg(tags.size() - shown);
        const QSize size = pillSizeFor(fontMetrics, overflow);
        paintPill(painter, QRect(x, top, size.width(), size.height()), option.palette.mid().color(), overflow, option.font);
    }

    painter->restore();
}

QSize TagsCellDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);
    const QFontMetrics fontMetrics(option.font);
    return QSize(option.rect.width(), fontMetrics.height() + 2 * kPillPaddingV + 2 * kRowMargin);
}
