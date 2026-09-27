#pragma once

#include <QObject>

#include "NotificationSettings.h"

class NotificationSettingsStore;

// Runs NotificationSettingsStore::save on whatever thread it has been moved to, the same
// "worker QObject moved to a dedicated QThread, dispatched via a queued functor" shape
// HighlightThemeSaveWorker/FileDecorationRulesSaveWorker already establish -- disk I/O must never
// run on the Qt UI thread.
class NotificationSettingsSaveWorker : public QObject
{
    Q_OBJECT

public:
    explicit NotificationSettingsSaveWorker(NotificationSettingsStore& store, QObject* parent = nullptr);

    void save(const NotificationSettings& settings) const;

private:
    NotificationSettingsStore& m_store;
};
