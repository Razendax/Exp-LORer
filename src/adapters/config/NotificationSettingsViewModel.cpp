#include "NotificationSettingsViewModel.h"

NotificationSettingsViewModel::NotificationSettingsViewModel(NotificationSettings& settings, NotificationSettingsStore& store,
                                                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
    , m_saveDispatcher([&store](const NotificationSettings& v) { store.save(v); })
{
}

void NotificationSettingsViewModel::setSettingsFor(NotificationType type, bool enabled, int timeoutMs)
{
    m_settings.setSettingsFor(type, enabled, timeoutMs);
    emit settingsChanged();
    m_saveDispatcher.dispatch(m_settings);
}
