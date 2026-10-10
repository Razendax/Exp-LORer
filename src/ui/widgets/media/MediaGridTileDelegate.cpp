#include "MediaGridTileDelegate.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QTime>

#include "FileListModel.h"
#include "FileSizeFormat.h"
#include "MediaExtensions.h"
#include "MediaPreviewFrames.h"
#include "MediaThumbnailData.h"
#include "MediaThumbnailPainting.h"

namespace
{
    constexpr int kPadding = 8;
    constexpr int kMinTextWidth = 60;
    constexpr int kChipPadding = 4;
    constexpr int kChipMargin = 6;

    // The row's Qt::FontRole (set by a decoration rule) if it has one, otherwise the view's
    // default font -- mirrors FileIconDelegate::resolvedFont so paint()/nameRectFor()/sizeHint()
    // all agree on the same font.
    QFont resolvedFont(const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        const QVariant fontData = index.data(Qt::FontRole);
        return fontData.canConvert<QFont>() ? fontData.value<QFont>() : option.font;
    }

    // Semi-transparent rounded-rect label chip anchored inside `bounds`'s bottom-left (alignRight ==
    // false) or bottom-right (alignRight == true) corner, ~4-6px padded (Architecture.md §14.62).
    // No-op for an empty label (e.g. an image whose dimensions aren't known yet).
    void paintCornerChip(QPainter* painter, const QRect& bounds, const QString& text, bool alignRight)
    {
        if (text.isEmpty())
        {
            return;
        }

        QFont font = painter->font();
        font.setPointSize(std::max(7, font.pointSize() - 2));
        const QFontMetrics metrics(font);
        const QSize textSize = metrics.size(Qt::TextSingleLine, text);

        const int chipWidth = textSize.width() + 2 * kChipPadding;
        const int chipHeight = textSize.height() + 2 * kChipPadding;
        const int left = alignRight ? bounds.right() - kChipMargin - chipWidth : bounds.left() + kChipMargin;
        const QRect chipRect(left, bounds.bottom() - kChipMargin - chipHeight, chipWidth, chipHeight);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 160));
        painter->drawRoundedRect(chipRect, 4, 4);

        painter->setFont(font);
        painter->setPen(Qt::white);
        painter->drawText(chipRect, Qt::AlignCenter, text);
        painter->restore();
    }
}

MediaGridTileDelegate::MediaGridTileDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QRect MediaGridTileDelegate::thumbnailRectForCellRect(const QRect& cellRect, const QSize& iconSize)
{
    return QRect(cellRect.left() + (cellRect.width() - iconSize.width()) / 2, cellRect.top() + kPadding, iconSize.width(),
                 iconSize.height());
}

QRect MediaGridTileDelegate::thumbnailRectFor(const QStyleOptionViewItem& option)
{
    return thumbnailRectForCellRect(option.rect, option.decorationSize);
}

void MediaGridTileDelegate::setHoveredPreview(const QModelIndex& index, int previewIndex)
{
    m_hoveredIndex = index;
    m_hoveredPreviewIndex = previewIndex;
}

QRect MediaGridTileDelegate::nameRectFor(const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const QRect iconRect = thumbnailRectFor(option);
    const QFontMetrics fontMetrics(resolvedFont(option, index));
    return QRect(option.rect.left() + kPadding, iconRect.bottom() + kPadding, option.rect.width() - 2 * kPadding, fontMetrics.height());
}

void MediaGridTileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    const QRect iconRect = thumbnailRectFor(option);
    const QVariant thumbnailVariant = index.data(FileListModel::MediaThumbnailRole);
    const bool hasThumbnail = thumbnailVariant.canConvert<MediaThumbnailData>()
                               && !thumbnailVariant.value<MediaThumbnailData>().image.isNull();

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

        MediaThumbnailPainting::paintThumbnail(painter, imageToPaint, iconRect);
    }
    else
    {
        MediaThumbnailPainting::paintPlaceholderIcon(painter, index.data(Qt::DecorationRole).value<QIcon>(), iconRect);
    }

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

    if (hasThumbnail)
    {
        const MediaThumbnailData data = thumbnailVariant.value<MediaThumbnailData>();

        const QString sizeLabel = FileSizeFormat::format(index.data(FileListModel::SizeBytesRole).toULongLong());
        paintCornerChip(painter, iconRect, sizeLabel, false);

        const auto path = std::filesystem::path(index.data(FileListModel::FilePathRole).toString().toStdWString());
        const std::string extension = MediaExtensions::lowercaseExtension(path);
        QString rightLabel;
        if (MediaExtensions::isImageExtension(extension))
        {
            if (data.width > 0 && data.height > 0)
            {
                rightLabel = QStringLiteral("%1×%2").arg(data.width).arg(data.height);
            }
        }
        else if (data.duration.count() > 0)
        {
            rightLabel = MediaThumbnailPainting::durationLabel(data.duration);
        }
        paintCornerChip(painter, iconRect, rightLabel, true);
    }

    if (option.state & QStyle::State_Selected)
    {
        QPen pen(Qt::blue, 1, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
    }

    painter->restore();
}

QSize MediaGridTileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QSize iconSize = option.decorationSize;
    const QFontMetrics fontMetrics(resolvedFont(option, index));

    const int width = std::max(iconSize.width(), kMinTextWidth) + 2 * kPadding;
    const int height = iconSize.height() + kPadding + fontMetrics.height() + kPadding;

    return QSize(width, height);
}
