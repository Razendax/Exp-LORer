#include "NotificationCenter.h"

#include "Logging.h"
#include "NotificationSettings.h"

namespace
{
    // Never title/body -- arbitrary caller-formatted text (Architecture.md §14.23).
    const char* toLogString(NotificationType type)
    {
        switch (type)
        {
            case NotificationType::Info: return "Info";
            case NotificationType::Warning: return "Warning";
            case NotificationType::Error: return "Error";
        }
        return "Unknown";
    }
}

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
        Logging::debug(std::string("NotificationCenter: popup suppressed (type=") + toLogString(type) + ")");
        return;
    }

    Logging::debug(std::string("NotificationCenter: popup shown (type=") + toLogString(type) + ")");
    emit popupRequested(type, title, body, typeSettings.timeoutMs);
}

NotificationCenter::ProgressId NotificationCenter::createProgressNotification(const QString& title, int barCount, bool cancellable)
{
    const ProgressId id = m_nextProgressId++;
    Logging::debug("NotificationCenter: progress notification created (id=" + std::to_string(id) + ", bars=" +
                    std::to_string(barCount) + ")");
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
    Logging::debug("NotificationCenter: progress notification terminated (id=" + std::to_string(id) + ")");
    emit progressNotificationTerminated(id);
}

void NotificationCenter::requestCancel(ProgressId id)
{
    Logging::debug("NotificationCenter: progress cancel requested (id=" + std::to_string(id) + ")");
    emit progressCancelRequested(id);
}
