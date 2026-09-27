#include "NotificationSettingsViewModel.h"

#include <QMetaObject>

#include "NotificationSettingsSaveWorker.h"

NotificationSettingsViewModel::NotificationSettingsViewModel(NotificationSettings& settings, NotificationSettingsStore& store,
                                                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
{
    m_saveWorker = new NotificationSettingsSaveWorker(m_store);
    m_saveWorker->moveToThread(&m_saveThread);
    m_saveThread.start();
}

NotificationSettingsViewModel::~NotificationSettingsViewModel()
{
    m_saveThread.quit();
    m_saveThread.wait();
    delete m_saveWorker;
}

void NotificationSettingsViewModel::setSettingsFor(NotificationType type, bool enabled, int timeoutMs)
{
    m_settings.setSettingsFor(type, enabled, timeoutMs);
    emit settingsChanged();

    // Save runs on m_saveThread -- NotificationSettings is a plain copyable value type, so the
    // worker gets its own independent snapshot rather than touching m_settings from another thread.
    QMetaObject::invokeMethod(
        m_saveWorker, [worker = m_saveWorker, settings = m_settings]() { worker->save(settings); }, Qt::QueuedConnection);
}
