#include "TagsCellDelegate.h"

#include <vector>

#include <QFontMetrics>
#include <QPainter>
#include <QSize>
#include <QStyle>
#include <QString>

#include "FileListModel.h"
#include "Tag.h"
#include "TagPillRowPainter.h"

namespace
{
    constexpr int kRowMargin = 4;
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

    TagPillRowPainter::paintTagPillRow(painter, option.rect.adjusted(kRowMargin, 0, -kRowMargin, 0), tags, option.font);

    painter->restore();
}

QSize TagsCellDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);
    const QFontMetrics fontMetrics(option.font);
    return QSize(option.rect.width(), TagPillRowPainter::tagPillRowHeight(fontMetrics) + 2 * kRowMargin);
}
