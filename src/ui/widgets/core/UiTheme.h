#pragma once

class QPalette;
class UiThemeChangeNotifier;

// Light/Dark app theme (Architecture.md §14.50). The whole app runs under Qt's Fusion style
// (installFusionStyle(), set once at startup) rather than the native Windows style, since native
// style mostly ignores QPalette for menus/scrollbars/combo popups and so can't go fully dark --
// Fusion makes Dark fully consistent at the cost of Light's native controls (buttons/scrollbars/
// menus) rendering in Fusion's flatter look instead of Windows-native bevels. Only the QPalette
// changes between Light and Dark; the style itself is never re-set.
namespace UiTheme
{
    enum class Theme
    {
        Light,
        Dark,
    };

    // Process-wide current value, persisted via QSettings (key "UI/Theme") -- lazily read from
    // QSettings on first call, then cached until the next setTheme() call.
    Theme currentTheme();

    // Installs Qt's Fusion style as the application style. Called exactly once at startup
    // (CompositionRoot/main.cpp), before the first paletteFor()/setTheme() call -- Fusion is the
    // style for both themes, only the palette flips.
    void installFusionStyle();

    // Fusion-based QPalette for `theme`: Light returns Fusion's own stock light palette (close
    // enough to the app's previous native-Windows-style colors to read as visually unchanged);
    // Dark is a hand-tuned dark palette with QPalette::Highlight kept close to the app's own
    // accent-blue hue (UiColors::current().accentBlue) rather than Fusion's default blue.
    QPalette paletteFor(Theme theme);

    // Persists `theme`, applies its QPalette to QApplication, and emits `notifier`.themeChanged()
    // so every open widget with a theme-aware stylesheet re-applies it live. `notifier` is injected
    // (the shared instance owned by CompositionRoot) rather than reached via a hidden global.
    void setTheme(Theme theme, UiThemeChangeNotifier& notifier);
}
