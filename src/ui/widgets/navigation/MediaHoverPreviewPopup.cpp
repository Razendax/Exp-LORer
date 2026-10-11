#include "MediaHoverPreviewPopup.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

namespace
{
    constexpr int kMaxPreviewSize = 360;
    constexpr int kBorderWidth = 1;
    constexpr int kCursorOffset = 16;
}

MediaHoverPreviewPopup::MediaHoverPreviewPopup(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mediaHoverPreviewPopup"));
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
}

void MediaHoverPreviewPopup::showPreview(const QPixmap& pixmap, const QPoint& globalAnchorPos)
{
    m_pixmap = pixmap.scaled(QSize(kMaxPreviewSize, kMaxPreviewSize), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    const QSize popupSize = m_pixmap.size() + QSize(2 * kBorderWidth, 2 * kBorderWidth);
    resize(popupSize);

    QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    if (const QScreen* screen = QGuiApplication::screenAt(globalAnchorPos))
    {
        screenGeometry = screen->geometry();
    }

    int x = globalAnchorPos.x() + kCursorOffset;
    if (x + popupSize.width() > screenGeometry.right())
    {
        x = globalAnchorPos.x() - kCursorOffset - popupSize.width();
    }

    int y = globalAnchorPos.y();
    if (y + popupSize.height() > screenGeometry.bottom())
    {
        y = screenGeometry.bottom() - popupSize.height();
    }

    move(QPoint(x, y));
    show();
    update();
}

void MediaHoverPreviewPopup::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setPen(palette().mid().color());
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.drawPixmap(kBorderWidth, kBorderWidth, m_pixmap);
}
