#pragma once

#include <QObject>

// Pure decoupling point (Architecture.md §14.50, Light/Dark theme), same shape as
// TagChangeNotifier: UiTheme::setTheme() emits themeChanged() after applying the new QPalette, and
// every widget whose own stylesheet embeds a UiColors::current() value connects to it to re-apply
// that stylesheet live -- without UiTheme needing to know about any of those widgets directly. One
// shared instance, owned by CompositionRoot.
class UiThemeChangeNotifier : public QObject
{
    Q_OBJECT

public:
    explicit UiThemeChangeNotifier(QObject* parent = nullptr);

signals:
    void themeChanged();
};
