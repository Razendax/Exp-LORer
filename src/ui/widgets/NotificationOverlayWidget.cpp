#include "NotificationOverlayWidget.h"

#include <QVBoxLayout>

#include "NotificationCenter.h"
#include "ProgressNotificationWidget.h"
#include "ToastNotificationWidget.h"

NotificationOverlayWidget::NotificationOverlayWidget(NotificationCenter& notificationCenter, QWidget* parent)
    : QWidget(parent)
    , m_notificationCenter(notificationCenter)
{
    setObjectName(QStringLiteral("notificationOverlayWidget"));

    // This widget spans the full right-hand strip of MainWindow so it can anchor cards to the
    // bottom-right corner, but it must not swallow clicks over the (usually empty) area where no
    // card is showing. Making it transparent to mouse events lets clicks fall through to whatever
    // is underneath; child cards (toasts, progress bars) are hit-tested before this flag applies,
    // so their own buttons/close "x" still receive input normally.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // Stretch first, packed cards after: newly-added cards land at the bottom of the layout
    // (nearest the bottom-right corner this widget is anchored to), pushing earlier cards upward.
    m_stackLayout = new QVBoxLayout(this);
    m_stackLayout->setContentsMargins(8, 8, 8, 8);
    m_stackLayout->setSpacing(6);
    m_stackLayout->addStretch(1);

    connect(&m_notificationCenter, &NotificationCenter::popupRequested, this, &NotificationOverlayWidget::onPopupRequested);
    connect(&m_notificationCenter, &NotificationCenter::progressNotificationCreated, this,
            &NotificationOverlayWidget::onProgressNotificationCreated);
    connect(&m_notificationCenter, &NotificationCenter::progressBarUpdated, this, &NotificationOverlayWidget::onProgressBarUpdated);
    connect(&m_notificationCenter, &NotificationCenter::progressNotificationTerminated, this,
            &NotificationOverlayWidget::onProgressNotificationTerminated);
}

void NotificationOverlayWidget::reposition(int availableHeight)
{
    if (!parentWidget())
    {
        return;
    }

    setGeometry(parentWidget()->width() - kWidth, 0, kWidth, availableHeight);
    raise();
}

void NotificationOverlayWidget::onPopupRequested(NotificationType type, const QString& title, const QString& body, int timeoutMs)
{
    auto* toast = new ToastNotificationWidget(type, title, body, timeoutMs, this);
    connect(toast, &ToastNotificationWidget::closeRequested, this, [this, toast]() {
        m_stackLayout->removeWidget(toast);
        toast->deleteLater();
    });
    m_stackLayout->addWidget(toast);
    toast->show();
}

void NotificationOverlayWidget::onProgressNotificationCreated(int id, const QString& title, int barCount)
{
    auto* progress = new ProgressNotificationWidget(title, barCount, this);
    m_progressWidgets.insert(id, progress);
    m_stackLayout->addWidget(progress);
    progress->show();
}

void NotificationOverlayWidget::onProgressBarUpdated(int id, int barIndex, int current, int max, const QString& text)
{
    if (ProgressNotificationWidget* progress = m_progressWidgets.value(id, nullptr))
    {
        progress->updateBar(barIndex, current, max, text);
    }
}

void NotificationOverlayWidget::onProgressNotificationTerminated(int id)
{
    if (ProgressNotificationWidget* progress = m_progressWidgets.take(id))
    {
        m_stackLayout->removeWidget(progress);
        progress->deleteLater();
    }
}
