#include "MediaDetailsDelegate.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QStyle>

#include "FileListModel.h"
#include "FileSizeFormat.h"
#include "MediaExtensions.h"
#include "MediaPreviewFrames.h"
#include "MediaThumbnailData.h"
#include "MediaThumbnailPainting.h"
#include "Tag.h"
#include "TagPillRowPainter.h"

namespace
{
    constexpr int kPadding = 8;
    constexpr int kLineSpacing = 4;
    constexpr int kTagsSpacing = 12;

    // Mirrors MediaGridTileDelegate's resolvedFont -- kept as its own copy, same posture as the
    // rest of this codebase's per-delegate layout math.
    QFont resolvedFont(const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        const QVariant fontData = index.data(Qt::FontRole);
        return fontData.canConvert<QFont>() ? fontData.value<QFont>() : option.font;
    }

    QFont nameFontFor(const QFont& baseFont)
    {
        QFont font = baseFont;
        font.setBold(true);
        font.setPointSize(font.pointSize() + 1);
        return font;
    }

    // duration | size | WxH -- the duration segment (and its separator) is omitted for an image
    // row, same MediaExtensions::isImageExtension branch MediaGridTileDelegate uses; the
    // resolution segment is omitted until known.
    QString metadataLineFor(const QModelIndex& index, const MediaThumbnailData& data)
    {
        const auto path = std::filesystem::path(index.data(FileListModel::FilePathRole).toString().toStdWString());
        const std::string extension = MediaExtensions::lowercaseExtension(path);
        const bool isImage = MediaExtensions::isImageExtension(extension);

        QStringList segments;
        if (!isImage && data.duration.count() > 0)
        {
            segments << MediaThumbnailPainting::durationLabel(data.duration);
        }
        segments << FileSizeFormat::format(index.data(FileListModel::SizeBytesRole).toULongLong());
        if (data.width > 0 && data.height > 0)
        {
            segments << QStringLiteral("%1×%2").arg(data.width).arg(data.height);
        }
        return segments.join(QStringLiteral(" | "));
    }
}

MediaDetailsDelegate::MediaDetailsDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QRect MediaDetailsDelegate::thumbnailRectForCellRect(const QRect& cellRect, const QSize& iconSize)
{
    const int side = std::min({ iconSize.width(), iconSize.height(), cellRect.height() - 2 * kPadding });
    return QRect(cellRect.left() + kPadding, cellRect.top() + (cellRect.height() - side) / 2, side, side);
}

QRect MediaDetailsDelegate::thumbnailRectFor(const QStyleOptionViewItem& option)
{
    return thumbnailRectForCellRect(option.rect, option.decorationSize);
}

void MediaDetailsDelegate::setHoveredPreview(const QModelIndex& index, int previewIndex)
{
    m_hoveredIndex = index;
    m_hoveredPreviewIndex = previewIndex;
}

void MediaDetailsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    const QRect thumbnailRect = thumbnailRectFor(option);
    const QVariant thumbnailVariant = index.data(FileListModel::MediaThumbnailRole);
    const bool hasThumbnail =
        thumbnailVariant.canConvert<MediaThumbnailData>() && !thumbnailVariant.value<MediaThumbnailData>().image.isNull();

    if (hasThumbnail)
    {
        const MediaThumbnailData& thumbnailData = thumbnailVariant.value<MediaThumbnailData>();

        QImage imageToPaint = thumbnailData.image;
        if (index == m_hoveredIndex && thumbnailData.previewFrames.size() == static_cast<std::size_t>(MediaPreviewFrames::kCount))
        {
            const int frameIndex = std::clamp(m_hoveredPreviewIndex, 0, MediaPreviewFrames::kCount - 1);
            const QImage& hoveredFrame = thumbnailData.previewFrames[static_cast<std::size_t>(frameIndex)];
            if (!hoveredFrame.isNull())
            {
                imageToPaint = hoveredFrame;
            }
        }

        MediaThumbnailPainting::paintThumbnail(painter, imageToPaint, thumbnailRect);
    }
    else
    {
        MediaThumbnailPainting::paintPlaceholderIcon(painter, index.data(Qt::DecorationRole).value<QIcon>(), thumbnailRect);
    }

    const QFont baseFont = resolvedFont(option, index);
    const QFont nameFont = nameFontFor(baseFont);
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics lineMetrics(baseFont);
    const int tagsRowHeight = TagPillRowPainter::tagPillRowHeight(lineMetrics);

    const int textLeft = thumbnailRect.right() + kPadding;
    const int textWidth = option.rect.right() - kPadding - textLeft;
    const int textTop = option.rect.top() + kPadding;

    const QVariant foreground = index.data(Qt::ForegroundRole);
    const QColor textColor = (!(option.state & QStyle::State_Selected) && foreground.canConvert<QColor>())
        ? foreground.value<QColor>()
        : option.palette.text().color();

    painter->setPen(textColor);
    painter->setFont(nameFont);
    const QRect nameRect(textLeft, textTop, textWidth, nameMetrics.height());
    const QString name = index.data(Qt::DisplayRole).toString();
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(name, Qt::ElideRight, nameRect.width()));

    const QRect metadataRect(textLeft, nameRect.bottom() + kLineSpacing, textWidth, lineMetrics.height());
    if (hasThumbnail)
    {
        painter->setFont(baseFont);
        painter->setPen(textColor);
        const QString metadataLine = metadataLineFor(index, thumbnailVariant.value<MediaThumbnailData>());
        painter->drawText(metadataRect, Qt::AlignLeft | Qt::AlignVCenter,
                           lineMetrics.elidedText(metadataLine, Qt::ElideRight, metadataRect.width()));
    }

    const QRect tagsRect(textLeft, metadataRect.bottom() + kTagsSpacing, textWidth, tagsRowHeight);
    const auto tags = index.data(FileListModel::TagsRole).value<std::vector<Tag>>();
    TagPillRowPainter::paintTagPillRow(painter, tagsRect, tags, baseFont);

    if (option.state & QStyle::State_Selected)
    {
        QPen pen(Qt::blue, 1, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
    }

    painter->restore();
}

QSize MediaDetailsDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QFont baseFont = resolvedFont(option, index);
    const QFontMetrics nameMetrics(nameFontFor(baseFont));
    const QFontMetrics lineMetrics(baseFont);
    const int textBlockHeight =
        nameMetrics.height() + kLineSpacing + lineMetrics.height() + kTagsSpacing + TagPillRowPainter::tagPillRowHeight(lineMetrics);

    const int height = std::max(option.decorationSize.height(), textBlockHeight) + 2 * kPadding;
    return QSize(option.rect.width(), height);
}
