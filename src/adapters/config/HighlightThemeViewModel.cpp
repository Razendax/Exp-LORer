#include "HighlightThemeViewModel.h"

#include <QMetaObject>

#include "HighlightThemeSaveWorker.h"

HighlightThemeViewModel::HighlightThemeViewModel(HighlightTheme& theme, HighlightThemeStore& store, QObject* parent)
    : QObject(parent)
    , m_theme(theme)
    , m_store(store)
{
    m_saveWorker = new HighlightThemeSaveWorker(m_store);
    m_saveWorker->moveToThread(&m_saveThread);
    m_saveThread.start();
}

HighlightThemeViewModel::~HighlightThemeViewModel()
{
    m_saveThread.quit();
    m_saveThread.wait();
    delete m_saveWorker;
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

    // Save runs on m_saveThread -- HighlightTheme is a plain copyable value type, so the worker
    // gets its own independent snapshot rather than touching m_theme from another thread.
    QMetaObject::invokeMethod(
        m_saveWorker, [worker = m_saveWorker, theme = m_theme]() { worker->save(theme); }, Qt::QueuedConnection);
}
