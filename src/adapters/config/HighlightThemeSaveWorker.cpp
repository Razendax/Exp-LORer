#include "HighlightThemeSaveWorker.h"

#include "HighlightThemeStore.h"

HighlightThemeSaveWorker::HighlightThemeSaveWorker(HighlightThemeStore& store, QObject* parent)
    : QObject(parent)
    , m_store(store)
{
}

void HighlightThemeSaveWorker::save(const HighlightTheme& theme) const
{
    m_store.save(theme);
}
