#pragma once

#include <filesystem>

#include "ColumnVisibilitySettings.h"

// Loads/saves ColumnVisibilitySettings as JSON via Qt's QJsonDocument -- same tolerant-load/
// atomic-save shape as NotificationSettingsStore, in its own column_visibility.json.
class ColumnVisibilitySettingsStore
{
public:
    explicit ColumnVisibilitySettingsStore(std::filesystem::path settingsFilePath);

    ColumnVisibilitySettings load() const;
    bool save(const ColumnVisibilitySettings& settings) const;

private:
    std::filesystem::path m_settingsFilePath;
};
