#pragma once

#include <QObject>
#include <QThread>

#include "NotificationSettings.h"
#include "NotificationSettingsStore.h"

class NotificationSettingsSaveWorker;

// Thin Qt-facing wrapper around NotificationSettings + NotificationSettingsStore, owned once by
// CompositionRoot like HighlightThemeViewModel/FileDecorationsViewModel so SettingsDialog's
// Notifications page is the single editor of the live settings -- an edit persists immediately.
// This is the *same* NotificationSettings instance NotificationCenter holds a const& to (both
// owned by CompositionRoot), so a Settings-page edit is visible to NotificationCenter on its very
// next call with no signal wiring needed between the two.
//
// Deliberately NOT depended on by src/adapters/viewmodels (TagListViewModel and friends): this
// class lives in src/adapters/config, which already depends on src/adapters/viewmodels, so the
// reverse dependency would be a build-graph cycle. TagListViewModel/WorkspaceController instead
// take NotificationCenter& directly (from src/adapters/notifications, which has no such
// dependency) -- see CompositionRoot for how the two are threaded together.
//
// The actual NotificationSettingsStore::save() call runs on a dedicated background thread (via
// NotificationSettingsSaveWorker), the same shape HighlightThemeViewModel/FileDecorationsViewModel
// use, so a Settings-page edit never blocks the Qt UI thread on disk I/O.
class NotificationSettingsViewModel : public QObject
{
    Q_OBJECT

public:
    NotificationSettingsViewModel(NotificationSettings& settings, NotificationSettingsStore& store, QObject* parent = nullptr);
    ~NotificationSettingsViewModel() override;

    NotificationTypeSettings settingsFor(NotificationType type) const { return m_settings.settingsFor(type); }
    void setSettingsFor(NotificationType type, bool enabled, int timeoutMs);

signals:
    void settingsChanged();

private:
    NotificationSettings& m_settings;
    NotificationSettingsStore& m_store;

    QThread m_saveThread;
    NotificationSettingsSaveWorker* m_saveWorker = nullptr;
};
