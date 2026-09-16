#pragma once

#include <filesystem>

#include "AppConfig.h"

// Loads/saves AppConfig as JSON via Qt's built-in QJsonDocument (Architecture.md §14.14) — no new
// vcpkg dependency, this module is the Qt/JSON persistence boundary (same posture as
// SQLiteTagRepository at its own boundary). Tolerant load: a missing file, unparsable JSON, or any
// individual malformed/missing field all fall back to that field's default rather than failing the
// whole load. Atomic save: writes to "<path>.tmp" then renames over the real file.
class AppConfigStore
{
public:
    explicit AppConfigStore(std::filesystem::path configFilePath);

    AppConfig load() const;
    bool save(const AppConfig& config) const;

private:
    std::filesystem::path m_configFilePath;
};
