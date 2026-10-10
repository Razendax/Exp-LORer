#include "MediaThumbnailPainting.h"

#include <QIcon>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QTime>

void MediaThumbnailPainting::paintThumbnail(QPainter* painter, const QImage& image, const QRect& rect)
{
    if (image.isNull())
    {
        return;
    }

    const QSize scaled = image.size().scaled(rect.size(), Qt::KeepAspectRatio);
    const QRect target(rect.left() + (rect.width() - scaled.width()) / 2, rect.top() + (rect.height() - scaled.height()) / 2,
                        scaled.width(), scaled.height());

    // Letterbox/pillarbox bars (when the image's aspect ratio doesn't match rect's) are painted
    // black rather than left as the view's background colour -- otherwise adjacent thumbnails
    // (e.g. Filmstrip's preview frames) show a visible seam of background colour between them.
    painter->fillRect(rect, Qt::black);
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

QString MediaThumbnailPainting::durationLabel(std::chrono::milliseconds duration)
{
    const auto totalSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    const QTime time = QTime(0, 0).addSecs(static_cast<int>(totalSeconds));
    const QString format = totalSeconds >= 3600 ? QStringLiteral("hh:mm:ss") : QStringLiteral("mm:ss");
    return time.toString(format);
}
