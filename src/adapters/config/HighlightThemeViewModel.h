#pragma once

#include <string>

#include <QColor>
#include <QObject>
#include <QString>

#include "AsyncSaveDispatcher.h"
#include "HighlightTheme.h"
#include "HighlightThemeStore.h"
#include "Language.h"

// Thin Qt-facing wrapper around HighlightTheme + HighlightThemeStore (Architecture.md §14.26),
// owned once by CompositionRoot/MainWindow like TagListViewModel so SettingsDialog and every
// SyntaxHighlighter observe the same live theme -- a recolor in Settings persists immediately and
// broadcasts themeChanged() so an already-open preview rehighlights without needing to reload.
// The actual HighlightThemeStore::save() call runs on a dedicated background thread (via
// m_saveDispatcher), so a color pick never blocks the Qt UI thread on disk I/O.
class HighlightThemeViewModel : public QObject
{
    Q_OBJECT

public:
    HighlightThemeViewModel(HighlightTheme& theme, HighlightThemeStore& store, QObject* parent = nullptr);

    // The color to show for `captureName` under `language`: the user's override if set, otherwise
    // HighlightTheme's built-in default. Invalid (unset) when neither exists -- callers should fall
    // back to the view's own default text color.
    QColor tokenColor(Language language, const std::string& captureName) const;

    void setTokenColor(Language language, const std::string& captureName, const QColor& color);

signals:
    void themeChanged();

private:
    HighlightTheme& m_theme;
    HighlightThemeStore& m_store;

    AsyncSaveDispatcher<HighlightTheme> m_saveDispatcher;
};
