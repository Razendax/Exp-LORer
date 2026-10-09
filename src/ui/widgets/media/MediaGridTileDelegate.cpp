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
#include "MediaThumbnailData.h"

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

    // Placeholder-icon fallback while a real thumbnail hasn't arrived yet -- copied from
    // FileIconDelegate::paintIcon (always stretches to fill `rect`, fine for a square system icon).
    void paintPlaceholderIcon(QPainter* painter, const QIcon& icon, const QRect& rect)
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

    // Aspect-fit, letterboxed inside `rect` -- unlike paintPlaceholderIcon() above (a square system
    // icon can always be stretched to fill), a real photo/poster-frame's own aspect ratio must be
    // preserved or it looks visibly distorted.
    void paintThumbnail(QPainter* painter, const QImage& image, const QRect& rect)
    {
        if (image.isNull())
        {
            return;
        }

        const QSize scaled = image.size().scaled(rect.size(), Qt::KeepAspectRatio);
        const QRect target(rect.left() + (rect.width() - scaled.width()) / 2, rect.top() + (rect.height() - scaled.height()) / 2,
                            scaled.width(), scaled.height());

        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(target, image);
    }

    // Same mm:ss/hh:mm:ss formatting as MediaPreviewWidget::setPreview.
    QString durationLabel(std::chrono::milliseconds duration)
    {
        const auto totalSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        const QTime time = QTime(0, 0).addSecs(static_cast<int>(totalSeconds));
        const QString format = totalSeconds >= 3600 ? QStringLiteral("hh:mm:ss") : QStringLiteral("mm:ss");
        return time.toString(format);
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

QRect MediaGridTileDelegate::thumbnailRectFor(const QStyleOptionViewItem& option)
{
    const QSize iconSize = option.decorationSize;
    return QRect(option.rect.left() + (option.rect.width() - iconSize.width()) / 2, option.rect.top() + kPadding, iconSize.width(),
                 iconSize.height());
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
        paintThumbnail(painter, thumbnailVariant.value<MediaThumbnailData>().image, iconRect);
    }
    else
    {
        paintPlaceholderIcon(painter, index.data(Qt::DecorationRole).value<QIcon>(), iconRect);
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
            rightLabel = durationLabel(data.duration);
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
