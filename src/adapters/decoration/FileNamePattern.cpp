#include "FileNamePattern.h"

#include "GlobMatch.h"

FileNamePattern::FileNamePattern(std::string pattern)
{
    if (!pattern.empty() && pattern.front() == '#')
    {
        m_appliesToFolders = true;
        pattern.erase(pattern.begin());
    }
    m_glob = std::move(pattern);
}

bool FileNamePattern::matches(const std::string& nameUtf8, bool isDirectory) const
{
    if (isDirectory != m_appliesToFolders)
    {
        return false;
    }

    return GlobMatch::matches(m_glob, nameUtf8);
}
