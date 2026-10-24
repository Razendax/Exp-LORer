#pragma once

// One enumerator per hotkey-catalog row (HotkeyCatalogEntry::action, Architecture.md §14.x).
// Append-only across milestones -- new rows add new enumerators at the end, existing ones are
// never reordered/renumbered. The underlying value itself doesn't need to stay stable (persistence
// keys off HotkeyCatalogEntry::stableId, never this enum's value -- see HotkeyBindings), but
// keeping it append-only avoids churning every other milestone's diff for no benefit.
enum class HotkeyAction
{
    // File Operations & Selection (spec §3.6) -- Milestone 1. Every one of these already exists as
    // a hardcoded shortcut in FileBrowserView::handleKeyPress today except SelectAll and
    // Properties, which gain real keyboard access for the first time here.
    Copy,
    Cut,
    Paste,
    Delete,
    DeletePermanently,
    Rename,
    NavigateToParent,
    OpenOrNavigateInto,
    SelectAll,
    Properties,

    // Navigation (spec §3.1) -- Milestone 2. All five are net-new keyboard access; none of these
    // chords collide with any Qt-internal QAbstractItemView default, so handleKeyPress just emits
    // on resolve (no extra swallow-logic needed, unlike SelectAll/OpenOrNavigateInto above).
    NavigateBack,
    NavigateForward,
    Refresh,
    GoToThisPC,
    FocusAddressBar,
};
