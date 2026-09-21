#include "FileDecorationRules.h"

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
