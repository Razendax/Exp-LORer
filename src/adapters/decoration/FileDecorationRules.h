#pragma once

#include <string>
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

private:
    std::vector<FileDecorationRule> m_rules;
};
