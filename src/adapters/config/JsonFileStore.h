#pragma once

#include <filesystem>
#include <optional>

#include <QJsonObject>

// Shared tolerant-load / atomic-save JSON-file helpers behind AppConfigStore and
// HighlightThemeStore (Architecture.md §14.14/§14.26) -- both persist a small JSON document via the
// same QFile-read/QJsonDocument::fromJson/QSaveFile-atomic-write shape, written once here instead of
// duplicated in each store.
namespace JsonFileStore
{
    // std::nullopt if the file doesn't exist, can't be opened, or isn't valid JSON holding an
    // object at the top level -- the caller falls back to defaults in every case, but only the
    // "exists and is malformed" case is logged (tagged `storeName`); a missing file is the expected
    // first run, not a warning.
    std::optional<QJsonObject> tryLoad(const std::filesystem::path& path, const char* storeName);

    // Writes `root` to `path` atomically (via QSaveFile), creating parent directories as needed.
    // Returns false (after logging, tagged `storeName`) on any I/O failure.
    bool trySave(const std::filesystem::path& path, const QJsonObject& root, const char* storeName);
}
