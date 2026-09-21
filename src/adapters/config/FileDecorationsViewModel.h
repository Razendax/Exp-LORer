#pragma once

#include <optional>
#include <string>
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

    // The rule that would win for `nameUtf8`/`isDirectory`, or std::nullopt if none matches -- a
    // copy, since FileDecorationRule is a plain value type.
    std::optional<FileDecorationRule> decorationFor(const std::string& nameUtf8, bool isDirectory) const;

    std::vector<FileDecorationRule> rules() const { return m_rules.rules(); }
    void setRules(std::vector<FileDecorationRule> rules);

signals:
    void rulesChanged();

private:
    FileDecorationRules& m_rules;
    FileDecorationRulesStore& m_store;

    QThread m_saveThread;
    FileDecorationRulesSaveWorker* m_saveWorker = nullptr;
};
