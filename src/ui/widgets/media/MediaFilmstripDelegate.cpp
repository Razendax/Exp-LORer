#include "MediaFilmstripDelegate.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QVariant>

#include "FileListModel.h"
#include "MediaPreviewFrames.h"
#include "MediaThumbnailData.h"
#include "MediaThumbnailPainting.h"

namespace
{
    constexpr int kPadding = 8;
    constexpr int kBandHeight = 120;

    // Mirrors MediaGridTileDelegate's resolvedFont -- kept as its own copy (small layout-rect-ish
    // helper, same posture as the rest of this codebase's per-delegate layout math).
    QFont resolvedFont(const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        const QVariant fontData = index.data(Qt::FontRole);
        return fontData.canConvert<QFont>() ? fontData.value<QFont>() : option.font;
    }

    QRect bandRectFor(const QStyleOptionViewItem& option)
    {
        return QRect(option.rect.left() + kPadding, option.rect.top() + kPadding, option.rect.width() - 2 * kPadding, kBandHeight);
    }

    QRect captionRectFor(const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        const QFontMetrics fontMetrics(resolvedFont(option, index));
        return MediaFilmstripDelegate::captionRectForCellRect(option.rect, fontMetrics);
    }
}

MediaFilmstripDelegate::MediaFilmstripDelegate(QObject* parent)
    : FileNameEditDelegate(parent)
{
}

QRect MediaFilmstripDelegate::captionRectForCellRect(const QRect& cellRect, const QFontMetrics& fontMetrics)
{
    const QRect bandRect(cellRect.left() + kPadding, cellRect.top() + kPadding, cellRect.width() - 2 * kPadding, kBandHeight);
    return QRect(cellRect.left() + kPadding, bandRect.bottom() + kPadding, cellRect.width() - 2 * kPadding, fontMetrics.height());
}

void MediaFilmstripDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    const QRect bandRect = bandRectFor(option);
    const QVariant thumbnailVariant = index.data(FileListModel::MediaThumbnailRole);
    const bool hasThumbnail =
        thumbnailVariant.canConvert<MediaThumbnailData>() && !thumbnailVariant.value<MediaThumbnailData>().image.isNull();

    if (hasThumbnail)
    {
        const MediaThumbnailData& thumbnailData = thumbnailVariant.value<MediaThumbnailData>();

        if (thumbnailData.previewFrames.size() == static_cast<std::size_t>(MediaPreviewFrames::kCount))
        {
            const int bandWidth = bandRect.width() / MediaPreviewFrames::kCount;
            for (int i = 0; i < MediaPreviewFrames::kCount; ++i)
            {
                const int left = bandRect.left() + i * bandWidth;
                const int width = (i == MediaPreviewFrames::kCount - 1) ? bandRect.right() - left + 1 : bandWidth;
                const QRect frameRect(left, bandRect.top(), width, bandRect.height());

                MediaThumbnailPainting::paintThumbnail(painter, thumbnailData.previewFrames[static_cast<std::size_t>(i)], frameRect);
            }
        }
        else
        {
            MediaThumbnailPainting::paintThumbnail(painter, thumbnailData.image, bandRect);
        }
    }
    else
    {
        MediaThumbnailPainting::paintPlaceholderIcon(painter, index.data(Qt::DecorationRole).value<QIcon>(), bandRect);
    }

    painter->setPen(QPen(option.palette.mid().color(), 1));
    painter->drawLine(bandRect.bottomLeft() + QPoint(0, kPadding / 2), bandRect.bottomRight() + QPoint(0, kPadding / 2));

    const QRect captionRect = captionRectFor(option, index);
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
    painter->drawText(captionRect, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics.elidedText(name, Qt::ElideRight, captionRect.width()));

    if (option.state & QStyle::State_Selected)
    {
        QPen pen(Qt::blue, 1, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
    }

    painter->restore();
}

QSize MediaFilmstripDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QFontMetrics fontMetrics(resolvedFont(option, index));
    const int height = kPadding + kBandHeight + kPadding + fontMetrics.height() + kPadding;
    return QSize(option.rect.width(), height);
}

void MediaFilmstripDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    editor->setGeometry(captionRectFor(option, index));
}
