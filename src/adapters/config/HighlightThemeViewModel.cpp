#include "HighlightThemeViewModel.h"

HighlightThemeViewModel::HighlightThemeViewModel(HighlightTheme& theme, HighlightThemeStore& store, QObject* parent)
    : QObject(parent)
    , m_theme(theme)
    , m_store(store)
    , m_saveDispatcher([&store](const HighlightTheme& v) { store.save(v); })
{
}

QColor HighlightThemeViewModel::tokenColor(Language language, const std::string& captureName) const
{
    if (const auto hexColor = m_theme.colorFor(language, captureName))
    {
        return QColor(QString::fromStdString(*hexColor));
    }
    return QColor();
}

void HighlightThemeViewModel::setTokenColor(Language language, const std::string& captureName, const QColor& color)
{
    m_theme.setTokenColor(language, captureName, color.name().toStdString());
    emit themeChanged();
    m_saveDispatcher.dispatch(m_theme);
}
