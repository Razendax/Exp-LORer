#include "UiTheme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

#include "UiThemeChangeNotifier.h"

namespace UiTheme
{
    namespace
    {
        constexpr const char* kThemeSettingsKey = "UI/Theme";
        constexpr const char* kDarkValue = "Dark";
        constexpr const char* kLightValue = "Light";

        Theme& currentThemeStorage()
        {
            static Theme value = [] {
                const QString stored = QSettings().value(QLatin1String(kThemeSettingsKey), QLatin1String(kLightValue)).toString();
                return stored == QLatin1String(kDarkValue) ? Theme::Dark : Theme::Light;
            }();
            return value;
        }
    }

    Theme currentTheme()
    {
        return currentThemeStorage();
    }

    void installFusionStyle()
    {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }

    QPalette paletteFor(Theme theme)
    {
        if (theme == Theme::Light)
        {
            return qApp->style()->standardPalette();
        }

        // Standard "Fusion dark palette" recipe, tuned by eye against the running app
        // (Architecture.md §14.50).
        QPalette dark;
        const QColor windowColor(0x2B, 0x2E, 0x33);
        const QColor baseColor(0x23, 0x25, 0x28);
        const QColor textColor(0xE0, 0xE0, 0xE0);
        const QColor disabledText(0x7A, 0x7A, 0x7A);
        const QColor buttonColor(0x3A, 0x3D, 0x42);
        const QColor highlightColor(0x4A, 0x77, 0xB4);

        dark.setColor(QPalette::Window, windowColor);
        dark.setColor(QPalette::WindowText, textColor);
        dark.setColor(QPalette::Base, baseColor);
        dark.setColor(QPalette::AlternateBase, windowColor);
        dark.setColor(QPalette::ToolTipBase, windowColor);
        dark.setColor(QPalette::ToolTipText, textColor);
        dark.setColor(QPalette::Text, textColor);
        dark.setColor(QPalette::Button, buttonColor);
        dark.setColor(QPalette::ButtonText, textColor);
        dark.setColor(QPalette::BrightText, Qt::red);
        dark.setColor(QPalette::Link, highlightColor);
        dark.setColor(QPalette::Highlight, highlightColor);
        dark.setColor(QPalette::HighlightedText, Qt::white);
        dark.setColor(QPalette::PlaceholderText, disabledText);

        dark.setColor(QPalette::Disabled, QPalette::Text, disabledText);
        dark.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
        dark.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
        dark.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x50, 0x50, 0x50));
        dark.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);

        return dark;
    }

    void setTheme(Theme theme, UiThemeChangeNotifier& notifier)
    {
        currentThemeStorage() = theme;
        QSettings().setValue(QLatin1String(kThemeSettingsKey), theme == Theme::Dark ? QLatin1String(kDarkValue) : QLatin1String(kLightValue));
        qApp->setPalette(paletteFor(theme));
        emit notifier.themeChanged();
    }
}
