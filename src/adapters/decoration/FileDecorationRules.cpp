#include "FileDecorationRules.h"

#include <optional>

FileDecorationRule FileDecorationRules::defaultHiddenRule()
{
    // Empty pattern list, no style override -- always succeeds (Architecture.md §14.29's "no
    // hardcoded dim color out of the box" default), so .value() is safe.
    return FileDecorationRule::create("", std::nullopt, std::nullopt, std::nullopt, false, false, false, false).value();
}

const FileDecorationRule* FileDecorationRules::resolve(const std::string& nameUtf8, bool isDirectory) const
{
    for (const auto& rule : m_rules)
    {
        if (rule.matches(nameUtf8, isDirectory))
        {
            return &rule;
        }
    }
    return nullptr;
}
