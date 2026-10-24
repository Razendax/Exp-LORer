#pragma once

#include <filesystem>

#include "HotkeyBindings.h"

// Loads/saves HotkeyBindings as JSON via Qt's QJsonDocument (Architecture.md §14.x) -- same
// tolerant-load/atomic-save shape as FavoritesStore, in its own hotkeys.json. Unknown "id"s (a
// downgrade, or a since-removed action) are skipped on load rather than erroring the whole file;
// an empty "chord" string round-trips as an explicit Clear override, not a malformed entry.
class HotkeyStore
{
public:
    explicit HotkeyStore(std::filesystem::path hotkeysFilePath);

    HotkeyBindings load() const;
    bool save(const HotkeyBindings& bindings) const;

private:
    std::filesystem::path m_hotkeysFilePath;
};
