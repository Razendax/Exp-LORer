#include "NotificationSettingsSaveWorker.h"

#include "NotificationSettingsStore.h"

NotificationSettingsSaveWorker::NotificationSettingsSaveWorker(NotificationSettingsStore& store, QObject* parent)
    : QObject(parent)
    , m_store(store)
{
}

void NotificationSettingsSaveWorker::save(const NotificationSettings& settings) const
{
    m_store.save(settings);
}
