#pragma once

#include <filesystem>
#include <vector>

#include "EnvironmentVariable.h"

// Loads/saves the custom per-process environment variable list as JSON via Qt's QJsonDocument
// (Architecture.md §14.57) -- same tolerant-load/atomic-save shape as FavoritesStore, in its own
// environment_variables.json.
class EnvironmentVariablesStore
{
public:
    explicit EnvironmentVariablesStore(std::filesystem::path environmentVariablesFilePath);

    std::vector<EnvironmentVariable> load() const;
    bool save(const std::vector<EnvironmentVariable>& variables) const;

private:
    std::filesystem::path m_environmentVariablesFilePath;
};
