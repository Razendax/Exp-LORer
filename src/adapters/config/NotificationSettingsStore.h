#pragma once

#include <filesystem>

#include "NotificationSettings.h"

// Loads/saves NotificationSettings as JSON via Qt's QJsonDocument -- same tolerant-load/
// atomic-save shape as HighlightThemeStore/FileDecorationRulesStore, in its own
// notification_settings.json (a persistent user preference edited from Settings and saved
// immediately, deliberately separate from config.json's session/window state).
class NotificationSettingsStore
{
public:
    explicit NotificationSettingsStore(std::filesystem::path settingsFilePath);

    NotificationSettings load() const;
    bool save(const NotificationSettings& settings) const;

private:
    std::filesystem::path m_settingsFilePath;
};
