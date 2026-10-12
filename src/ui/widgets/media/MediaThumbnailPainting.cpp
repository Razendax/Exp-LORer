#include "MediaThumbnailPainting.h"

#include <QBrush>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QTime>

namespace
{
    // Same 8px gray/white checkerboard VipsImageDecoder composites real transparency onto before
    // JPEG-encoding a raster thumbnail -- but QtSvgImageDecoder/QtGifImageDecoder (Architecture.md
    // §14.79/§14.81) keep their thumbnails' real alpha channel instead of flattening it, so a
    // transparent GIF/SVG with black content reaching here still needs *something* other than a
    // solid black fill behind it, or that content disappears into the fill (Architecture.md
    // §14.82). Built once as a small tiled QPixmap brush rather than per-pixel QImage::setPixel
    // calls on every paint.
    constexpr int kCheckerSquarePx = 8;
    constexpr int kCheckerLight = 255;
    constexpr int kCheckerDark = 205;

    const QPixmap& checkerboardBrushTile()
    {
        static const QPixmap tile = [] {
            QImage image(kCheckerSquarePx * 2, kCheckerSquarePx * 2, QImage::Format_RGB32);
            for (int y = 0; y < image.height(); ++y)
            {
                for (int x = 0; x < image.width(); ++x)
                {
                    const bool lightSquare = ((x / kCheckerSquarePx) + (y / kCheckerSquarePx)) % 2 == 0;
                    const int value = lightSquare ? kCheckerLight : kCheckerDark;
                    image.setPixel(x, y, qRgb(value, value, value));
                }
            }
            return QPixmap::fromImage(image);
        }();
        return tile;
    }
}

void MediaThumbnailPainting::paintThumbnail(QPainter* painter, const QImage& image, const QRect& rect)
{
    if (image.isNull())
    {
        return;
    }

    const QSize scaled = image.size().scaled(rect.size(), Qt::KeepAspectRatio);
    const QRect target(rect.left() + (rect.width() - scaled.width()) / 2, rect.top() + (rect.height() - scaled.height()) / 2,
                        scaled.width(), scaled.height());

    // Letterbox/pillarbox bars (when the image's aspect ratio doesn't match rect's) must be
    // painted something other than the view's own background colour -- otherwise adjacent
    // thumbnails (e.g. Filmstrip's preview frames) show a visible seam of background colour
    // between them. A video poster frame (or any other opaque thumbnail) never has an alpha
    // channel, so it keeps the previous plain black fill; a thumbnail with a real alpha channel
    // (an SVG/GIF decoded with transparency preserved, Architecture.md §14.79/§14.81) gets the
    // checkerboard instead, so transparent-but-dark content stays visible against it.
    if (image.hasAlphaChannel())
    {
        painter->fillRect(rect, QBrush(checkerboardBrushTile()));
    }
    else
    {
        painter->fillRect(rect, Qt::black);
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(target, image);
}

void MediaThumbnailPainting::paintPlaceholderIcon(QPainter* painter, const QIcon& icon, const QRect& rect)
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

void MediaThumbnailPainting::paintFolderPreviewCollage(QPainter* painter, const std::vector<QImage>& images, const QRect& rect)
{
    const int halfWidth = rect.width() / 2;
    const int halfHeight = rect.height() / 2;

    const QRect quadrants[4] = {
        QRect(rect.left(), rect.top(), halfWidth, halfHeight),
        QRect(rect.left() + halfWidth, rect.top(), rect.width() - halfWidth, halfHeight),
        QRect(rect.left(), rect.top() + halfHeight, halfWidth, rect.height() - halfHeight),
        QRect(rect.left() + halfWidth, rect.top() + halfHeight, rect.width() - halfWidth, rect.height() - halfHeight),
    };

    for (int i = 0; i < 4; ++i)
    {
        if (static_cast<std::size_t>(i) < images.size() && !images[static_cast<std::size_t>(i)].isNull())
        {
            paintThumbnail(painter, images[static_cast<std::size_t>(i)], quadrants[i]);
        }
        else
        {
            // No sample decoded for this quadrant yet -- a neutral placeholder square rather than
            // leaving it blank.
            painter->fillRect(quadrants[i], QColor(128, 128, 128, 60));
        }
    }
}

QString MediaThumbnailPainting::durationLabel(std::chrono::milliseconds duration)
{
    const auto totalSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    const QTime time = QTime(0, 0).addSecs(static_cast<int>(totalSeconds));
    const QString format = totalSeconds >= 3600 ? QStringLiteral("hh:mm:ss") : QStringLiteral("mm:ss");
    return time.toString(format);
}
