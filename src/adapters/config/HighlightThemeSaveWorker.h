#pragma once

#include <QObject>

#include "HighlightTheme.h"

class HighlightThemeStore;

// Runs HighlightThemeStore::save on whatever thread it has been moved to, the same
// "worker QObject moved to a dedicated QThread, dispatched via a queued functor" shape
// FilePreviewWorker/FilePreviewViewModel already establish (Architecture.md §5) -- disk I/O must
// never run on the Qt UI thread, and HighlightThemeViewModel::setTokenColor used to call
// HighlightThemeStore::save directly on the calling (UI) thread.
class HighlightThemeSaveWorker : public QObject
{
    Q_OBJECT

public:
    explicit HighlightThemeSaveWorker(HighlightThemeStore& store, QObject* parent = nullptr);

    void save(const HighlightTheme& theme) const;

private:
    HighlightThemeStore& m_store;
};
