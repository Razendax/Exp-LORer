#include "PaneTabBar.h"

#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>

PaneTabBar::PaneTabBar(WorkspacePaneId paneId, QWidget* parent)
    : QTabBar(parent)
    , m_paneId(paneId)
{
    setAcceptDrops(true);
}

void PaneTabBar::mousePressEvent(QMouseEvent* event)
{
    m_handingOffToDrag = false;
    QTabBar::mousePressEvent(event);
}

void PaneTabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_handingOffToDrag)
    {
        // Already handed off into a (blocking) QDrag::exec() -- nothing left to feed into the
        // now-irrelevant native in-bar drag.
        return;
    }

    if ((event->buttons() & Qt::LeftButton) && !rect().contains(event->position().toPoint()))
    {
        const int draggedIndex = currentIndex();
        if (draggedIndex < 0)
        {
            QTabBar::mouseMoveEvent(event);
            return;
        }

        m_handingOffToDrag = true;

        // Forces QTabBar's own native in-bar drag (already live from forwarding every prior
        // mouseMoveEvent below) to resolve cleanly -- reorder-or-snapback -- right at the moment it
        // would otherwise cross the bar's own edge, rather than leaving its drag indicator stuck at
        // the edge while an unrelated QDrag starts. See this class's header comment.
        QMouseEvent releaseEvent(QEvent::MouseButtonRelease, event->position(), event->globalPosition(), Qt::LeftButton,
                                  Qt::NoButton, event->modifiers());
        QTabBar::mouseReleaseEvent(&releaseEvent);

        beginCrossPaneDrag(draggedIndex);

        m_handingOffToDrag = false;
        return;
    }

    QTabBar::mouseMoveEvent(event);
}

void PaneTabBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_handingOffToDrag)
    {
        m_handingOffToDrag = false;
        return;
    }
    QTabBar::mouseReleaseEvent(event);
}

void PaneTabBar::beginCrossPaneDrag(int tabIndex)
{
    const DragPayload payload{ m_paneId, tabIndex, count() };

    auto* drag = new QDrag(this);
    drag->setMimeData(encode(payload));
    drag->exec(Qt::MoveAction);
}

int PaneTabBar::insertionIndexForDrop(const QPoint& pos) const
{
    const int hoveredIndex = tabAt(pos);
    if (hoveredIndex < 0)
    {
        return count();
    }
    return pos.x() > tabRect(hoveredIndex).center().x() ? hoveredIndex + 1 : hoveredIndex;
}

void PaneTabBar::dragEnterEvent(QDragEnterEvent* event)
{
    const std::optional<DragPayload> payload = decode(event->mimeData());
    if (!payload || payload->sourcePaneId == m_paneId || payload->sourceTabCountAtDragStart <= 1)
    {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void PaneTabBar::dragMoveEvent(QDragMoveEvent* event)
{
    const std::optional<DragPayload> payload = decode(event->mimeData());
    if (!payload || payload->sourcePaneId == m_paneId || payload->sourceTabCountAtDragStart <= 1)
    {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void PaneTabBar::dropEvent(QDropEvent* event)
{
    const std::optional<DragPayload> payload = decode(event->mimeData());
    if (!payload || payload->sourcePaneId == m_paneId || payload->sourceTabCountAtDragStart <= 1)
    {
        event->ignore();
        return;
    }

    const int destIndex = insertionIndexForDrop(event->position().toPoint());
    event->acceptProposedAction();
    emit tabDropped(payload->sourcePaneId, payload->sourceTabIndex, destIndex);
}

QMimeData* PaneTabBar::encode(const DragPayload& payload)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << static_cast<int>(payload.sourcePaneId) << payload.sourceTabIndex << payload.sourceTabCountAtDragStart;

    auto* mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(kMimeType), bytes);
    return mimeData;
}

std::optional<PaneTabBar::DragPayload> PaneTabBar::decode(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasFormat(QString::fromLatin1(kMimeType)))
    {
        return std::nullopt;
    }

    QByteArray bytes = mimeData->data(QString::fromLatin1(kMimeType));
    QDataStream stream(&bytes, QIODevice::ReadOnly);

    int sourcePaneId = 0;
    DragPayload payload;
    stream >> sourcePaneId >> payload.sourceTabIndex >> payload.sourceTabCountAtDragStart;
    payload.sourcePaneId = static_cast<WorkspacePaneId>(sourcePaneId);
    return payload;
}
