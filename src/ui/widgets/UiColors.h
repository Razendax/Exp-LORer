#pragma once

#include "UiTheme.h"

// Shared "bluish, muted" accent palette for panel/tab polish, theme-aware (Architecture.md
// §14.50): kLightColors holds the original fixed light-mode values unchanged, kDarkColors is a
// hand-tuned counterpart re-picked for contrast against UiTheme's dark QPalette, and current()
// returns whichever is active. Centralized so every consumer below stays visually consistent and
// retunable in one place.
namespace UiColors
{
    struct UiColorSet
    {
        const char* accentBlue;                       // active-tab underline, active-pane border, advanced search accent border
        const char* activeTabBackground;
        const char* navigationPanelBackground;         // every pane's toolbar/address-bar strip (item 7)
        const char* activePaneAccentBackground;         // focused pane's toolbar, layered on top of the baseline tint (item 3)
        const char* bottomPanelBackground;
        const char* sidePanelBackground;                // left and right side panels (SidePanelWidget)
        const char* advancedSearchButtonBackground;
        const char* advancedSearchButtonHoverBackground;
        const char* folderTagsBackground;
        const char* matchingTagsBackground;
        const char* selectedItemTagsBackground;

        // Advanced search "close" button -- deliberately a saturated red rather than muted, since
        // it needs to read as a distinct/destructive action rather than blend with the rest of the
        // palette.
        const char* closeButtonRed;
        const char* closeButtonRedHover;

        // Monitoring panel log rows (Architecture.md §14.24): legible-on-background saturation,
        // same register as closeButtonRed above, so Added/Removed/Modified read as distinct at a
        // glance in a fast-scrolling build log rather than blending into the muted panel-tint
        // palette.
        const char* monitorAddedGreen;
        const char* monitorRemovedRed;
        const char* monitorModifiedYellow;

        // Toast notification header strips (Info/Warning/Error) -- dedicated named constants even
        // though close in hue to some colors above, matching the existing convention of
        // separately-named same-hue constants for distinct semantic uses.
        const char* notificationInfoBlue;
        const char* notificationWarningYellow;
        const char* notificationErrorRed;
    };

    inline constexpr UiColorSet kLightColors{
        /* accentBlue */ "#507CBF",
        /* activeTabBackground */ "#C6CDD9",
        /* navigationPanelBackground */ "#D3D7DD",
        /* activePaneAccentBackground */ "#C9D1DD",
        /* bottomPanelBackground */ "#D1D4D8",
        /* sidePanelBackground */ "#D4D2D9",
        /* advancedSearchButtonBackground */ "#C6CDD9",
        /* advancedSearchButtonHoverBackground */ "#BAC6D8",
        /* folderTagsBackground */ "#CED8D0",
        /* matchingTagsBackground */ "#DDD6C8",
        /* selectedItemTagsBackground */ "#CDD3DD",
        /* closeButtonRed */ "#D9534F",
        /* closeButtonRedHover */ "#C9302C",
        /* monitorAddedGreen */ "#2E8B44",
        /* monitorRemovedRed */ "#D9534F",
        /* monitorModifiedYellow */ "#B8860B",
        /* notificationInfoBlue */ "#3B7DD8",
        /* notificationWarningYellow */ "#D9A441",
        /* notificationErrorRed */ "#D9534F",
    };

    // Dark counterpart (Architecture.md §14.50): muted-but-legible tints against UiTheme's dark
    // QPalette backgrounds rather than inverting hue; the five named semantic accents
    // (closeButtonRed, monitor*, notification*) keep their hue identity, brightened slightly for
    // dark-background contrast.
    inline constexpr UiColorSet kDarkColors{
        /* accentBlue */ "#6C9BD1",
        /* activeTabBackground */ "#3A4150",
        /* navigationPanelBackground */ "#32353B",
        /* activePaneAccentBackground */ "#3C4454",
        /* bottomPanelBackground */ "#2E3136",
        /* sidePanelBackground */ "#33343A",
        /* advancedSearchButtonBackground */ "#3A4150",
        /* advancedSearchButtonHoverBackground */ "#46506A",
        /* folderTagsBackground */ "#333B35",
        /* matchingTagsBackground */ "#3A362C",
        /* selectedItemTagsBackground */ "#333842",
        /* closeButtonRed */ "#E05C58",
        /* closeButtonRedHover */ "#C9433F",
        /* monitorAddedGreen */ "#4CAF60",
        /* monitorRemovedRed */ "#E05C58",
        /* monitorModifiedYellow */ "#D4A72C",
        /* notificationInfoBlue */ "#4A8EE0",
        /* notificationWarningYellow */ "#E0B84F",
        /* notificationErrorRed */ "#E05C58",
    };

    inline const UiColorSet& current()
    {
        return UiTheme::currentTheme() == UiTheme::Theme::Dark ? kDarkColors : kLightColors;
    }
}
