#include "FileDecorationsViewModel.h"

FileDecorationsViewModel::FileDecorationsViewModel(FileDecorationRules& rules, FileDecorationRulesStore& store, QObject* parent)
    : QObject(parent)
    , m_rules(rules)
    , m_store(store)
    , m_saveDispatcher([&store](const FileDecorationRules& v) { store.save(v); })
{
}

void FileDecorationsViewModel::setRules(std::vector<FileDecorationRule> rules)
{
    applyAndSave([&rules](FileDecorationRules& target) { target.setRules(std::move(rules)); });
}

void FileDecorationsViewModel::setHiddenFilesRule(FileDecorationRule rule)
{
    applyAndSave([&rule](FileDecorationRules& target) { target.setHiddenFilesRule(std::move(rule)); });
}

void FileDecorationsViewModel::setHiddenFoldersRule(FileDecorationRule rule)
{
    applyAndSave([&rule](FileDecorationRules& target) { target.setHiddenFoldersRule(std::move(rule)); });
}

void FileDecorationsViewModel::applyAndSave(std::function<void(FileDecorationRules&)> mutate)
{
    mutate(m_rules);
    emit rulesChanged();
    m_saveDispatcher.dispatch(m_rules);
}
