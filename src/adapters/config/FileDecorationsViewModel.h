#pragma once

#include <functional>
#include <vector>

#include <QObject>
#include <QThread>

#include "FileDecorationRule.h"
#include "FileDecorationRules.h"
#include "FileDecorationRulesStore.h"

class FileDecorationRulesSaveWorker;

// Thin Qt-facing wrapper around FileDecorationRules + FileDecorationRulesStore
// (Architecture.md §14.29), owned once by CompositionRoot like HighlightThemeViewModel so
// SettingsDialog's Colors page is the single editor of the live rule list -- a rule add/edit/
// reorder/remove persists immediately and broadcasts rulesChanged() so every open FileListModel
// restyles without needing to reload. The actual FileDecorationRulesStore::save() call runs on a
// dedicated background thread (via FileDecorationRulesSaveWorker), the same shape
// HighlightThemeViewModel uses, so an edit never blocks the Qt UI thread on disk I/O.
//
// Deliberately NOT depended on by src/adapters/viewmodels (FileListModel and friends): this class
// lives in src/adapters/config, which already depends on src/adapters/viewmodels (AppConfig needs
// FileListModel::ColumnCount), so the reverse dependency would be a build-graph cycle. FileListModel
// instead takes the plain FileDecorationRules& directly (from src/adapters/decoration, which has no
// such dependency) for matching, plus this class as a bare QObject& to connect to rulesChanged()
// via the string-based SIGNAL/SLOT overload of connect(), which needs no compile-time knowledge of
// this type. See CompositionRoot for how the two are threaded together.
class FileDecorationsViewModel : public QObject
{
    Q_OBJECT

public:
    FileDecorationsViewModel(FileDecorationRules& rules, FileDecorationRulesStore& store, QObject* parent = nullptr);
    ~FileDecorationsViewModel() override;

    std::vector<FileDecorationRule> rules() const { return m_rules.rules(); }
    void setRules(std::vector<FileDecorationRule> rules);

    // Built-in Hidden Files/Hidden Folders rows (Architecture.md §14.29) -- edited separately from
    // the pattern-rule list, same immediate-apply-and-save posture as setRules().
    FileDecorationRule hiddenFilesRule() const { return m_rules.hiddenFilesRule(); }
    FileDecorationRule hiddenFoldersRule() const { return m_rules.hiddenFoldersRule(); }
    void setHiddenFilesRule(FileDecorationRule rule);
    void setHiddenFoldersRule(FileDecorationRule rule);

signals:
    void rulesChanged();

private:
    // Common "mutate m_rules -> emit rulesChanged() -> queue a background save" tail shared by
    // setRules()/setHiddenFilesRule()/setHiddenFoldersRule().
    void applyAndSave(std::function<void(FileDecorationRules&)> mutate);

    FileDecorationRules& m_rules;
    FileDecorationRulesStore& m_store;

    QThread m_saveThread;
    FileDecorationRulesSaveWorker* m_saveWorker = nullptr;
};
