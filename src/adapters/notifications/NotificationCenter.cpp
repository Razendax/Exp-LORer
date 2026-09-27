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

NotificationCenter::ProgressId NotificationCenter::createProgressNotification(const QString& title, int barCount)
{
    const ProgressId id = m_nextProgressId++;
    emit progressNotificationCreated(id, title, barCount);
    return id;
}

void NotificationCenter::updateProgressBar(ProgressId id, int barIndex, int current, int max, const QString& text)
{
    emit progressBarUpdated(id, barIndex, current, max, text);
}

void NotificationCenter::terminateProgressNotification(ProgressId id)
{
    emit progressNotificationTerminated(id);
}
