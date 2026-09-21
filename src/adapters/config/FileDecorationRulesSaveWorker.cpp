#include "FileDecorationRulesSaveWorker.h"

#include "FileDecorationRulesStore.h"

FileDecorationRulesSaveWorker::FileDecorationRulesSaveWorker(FileDecorationRulesStore& store, QObject* parent)
    : QObject(parent)
    , m_store(store)
{
}

void FileDecorationRulesSaveWorker::save(const FileDecorationRules& rules) const
{
    m_store.save(rules);
}
