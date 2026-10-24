#pragma once

#include <string>

#include <QString>

#include "HotkeyAction.h"
#include "HotkeyChord.h"

// One rebindable row in the hotkey catalog (Architecture.md §14.x) -- a hand-written static table
// of these lives in HotkeyCatalog::entries(), one block of rows appended per milestone.
class HotkeyCatalogEntry
{
public:
    // Matches spec §3's six catalog subsections -- also SettingsHotkeysPage's category headers.
    enum class Category
    {
        Navigation,
        Tabs,
        PanesAndLayout,
        Panels,
        SearchAndTools,
        FileOperations,
    };

    // Where this action is dispatched from (shared design, Architecture.md §14.x). View requires a
    // file browser view to have focus (FileBrowserView::handleKeyPress); Window fires regardless of
    // which widget inside the main window currently has focus (HotkeyWindowDispatcher, built in a
    // later milestone).
    enum class Scope
    {
        View,
        Window,
    };

    HotkeyAction action = HotkeyAction::Copy;

    // Stable JSON key for hotkeys.json overrides (HotkeyBindings/HotkeyStore) -- never the enum
    // value, so a future HotkeyAction insertion/reorder can't silently remap an existing user's
    // saved override onto a different action.
    std::string stableId;

    Category category = Category::FileOperations;
    QString displayName;
    HotkeyChord defaultChord;
    Scope scope = Scope::View;

    // True only for the handful of actions spec §4 explicitly allows rebinding to a bare
    // (no-modifier) key -- every other action rejects a candidate chord with no Ctrl/Alt/Shift/Meta
    // modifier at all (accidental plain-letter rebinds).
    bool allowsBareRebind = false;
};
