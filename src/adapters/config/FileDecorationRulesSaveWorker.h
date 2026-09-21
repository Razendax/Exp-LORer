#pragma once

#include <QObject>

#include "FileDecorationRules.h"

class FileDecorationRulesStore;

// Runs FileDecorationRulesStore::save on whatever thread it has been moved to, the same
// "worker QObject moved to a dedicated QThread, dispatched via a queued functor" shape
// HighlightThemeSaveWorker already establishes -- disk I/O must never run on the Qt UI thread.
class FileDecorationRulesSaveWorker : public QObject
{
    Q_OBJECT

public:
    explicit FileDecorationRulesSaveWorker(FileDecorationRulesStore& store, QObject* parent = nullptr);

    void save(const FileDecorationRules& rules) const;

private:
    FileDecorationRulesStore& m_store;
};
