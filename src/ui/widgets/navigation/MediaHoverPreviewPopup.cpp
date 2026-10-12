#include "MediaHoverPreviewPopup.h"

#include <QGuiApplication>
#include <QHideEvent>
#include <QMovie>
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
    if (m_movie)
    {
        m_movie->stop();
    }

    m_pixmap = pixmap.scaled(QSize(kMaxPreviewSize, kMaxPreviewSize), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    positionAndShow(m_pixmap.size(), globalAnchorPos);
}

void MediaHoverPreviewPopup::showAnimatedGif(const std::filesystem::path& path, const QPoint& globalAnchorPos)
{
    m_movieAnchorPos = globalAnchorPos;

    if (!m_movie)
    {
        m_movie = new QMovie(this);
        connect(m_movie, &QMovie::frameChanged, this, [this](int) {
            m_pixmap = m_movie->currentPixmap().scaled(QSize(kMaxPreviewSize, kMaxPreviewSize), Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation);
            positionAndShow(m_pixmap.size(), m_movieAnchorPos);
        });
    }

    m_movie->stop();
    m_movie->setFileName(QString::fromStdWString(path.wstring()));
    m_movie->start();
}

void MediaHoverPreviewPopup::positionAndShow(const QSize& contentSize, const QPoint& globalAnchorPos)
{
    const QSize popupSize = contentSize + QSize(2 * kBorderWidth, 2 * kBorderWidth);
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

void MediaHoverPreviewPopup::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (m_movie)
    {
        m_movie->stop();
    }
}
