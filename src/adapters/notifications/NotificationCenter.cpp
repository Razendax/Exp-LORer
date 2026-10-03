#include "NotificationCenter.h"

#include "NotificationSettings.h"

NotificationCenter::NotificationCenter(const NotificationSettings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

void NotificationCenter::showPopup(NotificationType type, const QString& title, const QString& body)
{
    const NotificationTypeSettings typeSettings = m_settings.settingsFor(type);
    if (!typeSettings.enabled)
    {
        return;
    }

    emit popupRequested(type, title, body, typeSettings.timeoutMs);
}

NotificationCenter::ProgressId NotificationCenter::createProgressNotification(const QString& title, int barCount, bool cancellable)
{
    const ProgressId id = m_nextProgressId++;
    emit progressNotificationCreated(id, title, barCount, cancellable);
    return id;
}

void NotificationCenter::updateProgressBar(ProgressId id, int barIndex, int current, int max, const QString& aboveText,
                                            const QString& progressText, const QString& belowText)
{
    emit progressBarUpdated(id, barIndex, current, max, aboveText, progressText, belowText);
}

void NotificationCenter::terminateProgressNotification(ProgressId id)
{
    emit progressNotificationTerminated(id);
}

void NotificationCenter::requestCancel(ProgressId id)
{
    emit progressCancelRequested(id);
}
