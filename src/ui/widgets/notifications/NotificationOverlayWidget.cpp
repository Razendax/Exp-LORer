#include "NotificationOverlayWidget.h"

#include <algorithm>

#include <QVBoxLayout>

#include "NotificationCenter.h"
#include "ProgressNotificationWidget.h"
#include "ToastNotificationWidget.h"

NotificationOverlayWidget::NotificationOverlayWidget(NotificationCenter& notificationCenter, QWidget* parent)
    : QWidget(parent)
    , m_notificationCenter(notificationCenter)
{
    setObjectName(QStringLiteral("notificationOverlayWidget"));

    // Newly-added cards are appended to the bottom of the layout; combined with updateGeometry()
    // re-anchoring this widget's bottom edge to m_availableHeight on every add/remove, that makes
    // new cards land nearest the bottom-right corner and pushes earlier cards upward as the stack
    // grows, without needing a stretch spacer or a tall, mostly-empty widget.
    m_stackLayout = new QVBoxLayout(this);
    m_stackLayout->setContentsMargins(8, 8, 8, 8);
    m_stackLayout->setSpacing(6);

    connect(&m_notificationCenter, &NotificationCenter::popupRequested, this, &NotificationOverlayWidget::onPopupRequested);
    connect(&m_notificationCenter, &NotificationCenter::progressNotificationCreated, this,
            &NotificationOverlayWidget::onProgressNotificationCreated);
    connect(&m_notificationCenter, &NotificationCenter::progressBarUpdated, this, &NotificationOverlayWidget::onProgressBarUpdated);
    connect(&m_notificationCenter, &NotificationCenter::progressNotificationTerminated, this,
            &NotificationOverlayWidget::onProgressNotificationTerminated);
}

void NotificationOverlayWidget::reposition(int availableHeight)
{
    m_availableHeight = availableHeight;
    updateGeometry();
}

void NotificationOverlayWidget::updateGeometry()
{
    if (!parentWidget())
    {
        return;
    }

    // totalHeightForWidth() returns -1 when none of the current cards' items have a
    // width-dependent height (e.g. a lone ProgressNotificationWidget, whose labels don't
    // word-wrap) -- fall back to totalSizeHint() in that case, since blindly min()-ing a -1
    // against m_availableHeight would collapse this widget (and everything in it) to nothing.
    const int contentHeight = m_stackLayout->hasHeightForWidth() ? m_stackLayout->totalHeightForWidth(kWidth)
                                                                   : m_stackLayout->totalSizeHint().height();
    const int height = std::min(std::max(contentHeight, 0), m_availableHeight);
    setGeometry(parentWidget()->width() - kWidth, m_availableHeight - height, kWidth, height);
    raise();
}

void NotificationOverlayWidget::onPopupRequested(NotificationType type, const QString& title, const QString& body, int timeoutMs)
{
    auto* toast = new ToastNotificationWidget(type, title, body, timeoutMs, this);
    connect(toast, &ToastNotificationWidget::closeRequested, this, [this, toast]() {
        m_stackLayout->removeWidget(toast);
        toast->deleteLater();
        updateGeometry();
    });
    m_stackLayout->addWidget(toast);
    toast->show();
    updateGeometry();
}

void NotificationOverlayWidget::onProgressNotificationCreated(int id, const QString& title, int barCount, bool cancellable)
{
    auto* progress = new ProgressNotificationWidget(title, barCount, cancellable, this);
    connect(progress, &ProgressNotificationWidget::cancelRequested, this,
            [this, id]() { m_notificationCenter.requestCancel(id); });
    m_progressWidgets.insert(id, progress);
    m_stackLayout->addWidget(progress);
    progress->show();
    updateGeometry();
}

void NotificationOverlayWidget::onProgressBarUpdated(int id, int barIndex, int current, int max, const QString& aboveText,
                                                       const QString& progressText, const QString& belowText)
{
    if (ProgressNotificationWidget* progress = m_progressWidgets.value(id, nullptr))
    {
        progress->updateBar(barIndex, current, max, aboveText, progressText, belowText);
    }
}

void NotificationOverlayWidget::onProgressNotificationTerminated(int id)
{
    if (ProgressNotificationWidget* progress = m_progressWidgets.take(id))
    {
        m_stackLayout->removeWidget(progress);
        progress->deleteLater();
        updateGeometry();
    }
}
