#pragma once

// Shared "bluish, muted" accent palette for panel/tab polish (fixed light-mode values -- the app
// has no dark-mode/theme system, Architecture.md has no theming section). Centralized so every
// consumer below stays visually consistent and retunable in one place.
namespace UiColors
{
    constexpr const char* kAccentBlue = "#507CBF";                  // active-tab underline, active-pane border, advanced search accent border
    constexpr const char* kActiveTabBackground = "#C6CDD9";
    constexpr const char* kNavigationPanelBackground = "#D3D7DD";   // every pane's toolbar/address-bar strip (item 7)
    constexpr const char* kActivePaneAccentBackground = "#C9D1DD";  // focused pane's toolbar, layered on top of the baseline tint (item 3)
    constexpr const char* kBottomPanelBackground = "#D1D4D8";
    constexpr const char* kRightPanelBackground = "#D4D2D9";
    constexpr const char* kAdvancedSearchButtonBackground = "#C6CDD9";
    constexpr const char* kAdvancedSearchButtonHoverBackground = "#BAC6D8";
    constexpr const char* kFolderTagsBackground = "#CED8D0";
    constexpr const char* kMatchingTagsBackground = "#DDD6C8";
    constexpr const char* kSelectedItemTagsBackground = "#CDD3DD";

    // Advanced search "close" button -- deliberately a saturated red rather than muted, since it
    // needs to read as a distinct/destructive action rather than blend with the rest of the palette.
    constexpr const char* kCloseButtonRed = "#D9534F";
    constexpr const char* kCloseButtonRedHover = "#C9302C";
}
