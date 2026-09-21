#pragma once

#include <string>
#include <utility>
#include <vector>

#include "FileDecorationRule.h"

// Ordered list of FileDecorationRule -- order is priority (Architecture.md §14.29): the first
// rule whose pattern list matches a given name wins. Pure C++ wrapper, mirroring how
// LanguageRegistry is a pure lookup with no Qt/state beyond the table itself; here the "table" is
// user-editable and lives in FileDecorationsViewModel/FileDecorationRulesStore.
class FileDecorationRules
{
public:
    // First matching rule in list order, or nullptr if none matches.
    const FileDecorationRule* resolve(const std::string& nameUtf8, bool isDirectory) const;

    const std::vector<FileDecorationRule>& rules() const noexcept { return m_rules; }
    void setRules(std::vector<FileDecorationRule> rules) { m_rules = std::move(rules); }

    // Built-in, non-removable styling for hidden files/folders (Architecture.md §14.29) -- stored
    // separately from m_rules so they're structurally excluded from resolve()'s pattern matching
    // and can't be reordered relative to the pattern-rule list. FileListModel falls back to these
    // only when no pattern rule already matched.
    const FileDecorationRule& hiddenStyle(bool isDirectory) const noexcept { return isDirectory ? m_hiddenFolders : m_hiddenFiles; }
    const FileDecorationRule& hiddenFilesRule() const noexcept { return m_hiddenFiles; }
    const FileDecorationRule& hiddenFoldersRule() const noexcept { return m_hiddenFolders; }
    void setHiddenFilesRule(FileDecorationRule rule) { m_hiddenFiles = std::move(rule); }
    void setHiddenFoldersRule(FileDecorationRule rule) { m_hiddenFolders = std::move(rule); }

private:
    static FileDecorationRule defaultHiddenRule();

    std::vector<FileDecorationRule> m_rules;
    FileDecorationRule m_hiddenFiles = defaultHiddenRule();
    FileDecorationRule m_hiddenFolders = defaultHiddenRule();
};
