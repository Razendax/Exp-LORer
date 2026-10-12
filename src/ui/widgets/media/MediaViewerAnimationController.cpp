#include "MediaViewerAnimationController.h"

#include <QListView>
#include <QMovie>
#include <QScrollBar>

MediaViewerAnimationController::MediaViewerAnimationController(QListView* view, QObject* parent)
    : QObject(parent)
    , m_view(view)
{
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &MediaViewerAnimationController::pruneOffscreen);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MediaViewerAnimationController::pruneOffscreen);

    if (QAbstractItemModel* model = m_view->model())
    {
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, &MediaViewerAnimationController::clear);
        connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, &MediaViewerAnimationController::clear);
        connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, &MediaViewerAnimationController::clear);
    }
}

void MediaViewerAnimationController::setMode(MediaViewerPreviewMode mode)
{
    if (mode == m_mode)
    {
        return;
    }

    m_mode = mode;
    if (m_mode == MediaViewerPreviewMode::Static)
    {
        clear();
        m_view->viewport()->update();
    }
}

void MediaViewerAnimationController::stopAllForDelegateSwap()
{
    clear();
}

void MediaViewerAnimationController::setHoveredIndex(const QModelIndex& index)
{
    m_hoveredIndex = index;
}

QImage MediaViewerAnimationController::frameIfAnimating(const QPersistentModelIndex& index, const std::filesystem::path& filePath)
{
    const bool shouldAnimate = m_mode == MediaViewerPreviewMode::Autoplay
        || (m_mode == MediaViewerPreviewMode::AnimateOnHover && index == m_hoveredIndex);

    if (!shouldAnimate)
    {
        if (QMovie* movie = m_activeMovies.take(index))
        {
            movie->stop();
            movie->deleteLater();
        }
        return {};
    }

    QMovie* movie = m_activeMovies.value(index);
    if (!movie)
    {
        movie = new QMovie(this);
        movie->setFileName(QString::fromStdWString(filePath.wstring()));
        connect(movie, &QMovie::frameChanged, this, [this]() { m_view->viewport()->update(); });
        movie->start();
        m_activeMovies.insert(index, movie);
    }

    return movie->currentImage();
}

void MediaViewerAnimationController::pruneOffscreen()
{
    const QRect viewportRect = m_view->viewport()->rect();
    for (auto it = m_activeMovies.begin(); it != m_activeMovies.end();)
    {
        if (!viewportRect.intersects(m_view->visualRect(it.key())))
        {
            it.value()->stop();
            it.value()->deleteLater();
            it = m_activeMovies.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void MediaViewerAnimationController::clear()
{
    for (QMovie* movie : std::as_const(m_activeMovies))
    {
        movie->stop();
        movie->deleteLater();
    }
    m_activeMovies.clear();
}
