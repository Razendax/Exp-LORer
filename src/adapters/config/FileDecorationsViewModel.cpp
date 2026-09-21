#include "FileDecorationsViewModel.h"

#include <QMetaObject>

#include "FileDecorationRulesSaveWorker.h"

FileDecorationsViewModel::FileDecorationsViewModel(FileDecorationRules& rules, FileDecorationRulesStore& store, QObject* parent)
    : QObject(parent)
    , m_rules(rules)
    , m_store(store)
{
    m_saveWorker = new FileDecorationRulesSaveWorker(m_store);
    m_saveWorker->moveToThread(&m_saveThread);
    m_saveThread.start();
}

FileDecorationsViewModel::~FileDecorationsViewModel()
{
    m_saveThread.quit();
    m_saveThread.wait();
    delete m_saveWorker;
}

std::optional<FileDecorationRule> FileDecorationsViewModel::decorationFor(const std::string& nameUtf8, bool isDirectory) const
{
    if (const FileDecorationRule* rule = m_rules.resolve(nameUtf8, isDirectory))
    {
        return *rule;
    }
    return std::nullopt;
}

void FileDecorationsViewModel::setRules(std::vector<FileDecorationRule> rules)
{
    m_rules.setRules(std::move(rules));
    emit rulesChanged();

    // Save runs on m_saveThread -- FileDecorationRules is a plain copyable value type, so the
    // worker gets its own independent snapshot rather than touching m_rules from another thread.
    QMetaObject::invokeMethod(
        m_saveWorker, [worker = m_saveWorker, rules = m_rules]() { worker->save(rules); }, Qt::QueuedConnection);
}
