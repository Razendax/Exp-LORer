#include "EnvironmentVariableApplier.h"

#include <utility>

#ifdef _WIN32
#include <cwchar>

#include <Windows.h>
#endif

void EnvironmentVariableApplier::apply(const std::vector<EnvironmentVariable>& variables,
                                        const std::vector<QString>& previouslyAppliedNames)
{
#ifdef _WIN32
    for (const QString& name : previouslyAppliedNames)
    {
        SetEnvironmentVariableW(reinterpret_cast<const wchar_t*>(name.utf16()), nullptr);
    }

    for (const EnvironmentVariable& variable : variables)
    {
        const QString joinedValue = joinEnvironmentVariableValues(variable.values);
        SetEnvironmentVariableW(reinterpret_cast<const wchar_t*>(variable.name.utf16()),
                                reinterpret_cast<const wchar_t*>(joinedValue.utf16()));
    }
#else
    (void)variables;
    (void)previouslyAppliedNames;
#endif
}

std::vector<EnvironmentVariable> EnvironmentVariableApplier::currentProcessEnvironmentSnapshot()
{
    std::vector<EnvironmentVariable> variables;

#ifdef _WIN32
    LPWCH environmentBlock = GetEnvironmentStringsW();
    if (!environmentBlock)
    {
        return variables;
    }

    for (LPWCH entry = environmentBlock; *entry != L'\0'; entry += wcslen(entry) + 1)
    {
        const QString raw = QString::fromWCharArray(entry);

        // Windows environment blocks include hidden per-drive pseudo-variables whose "name" itself
        // starts with "=" (e.g. "=C:=C:\Users\..."), used internally to track each drive's current
        // directory -- not something a user ever meant to set/edit, so skip them. Searching for the
        // separating "=" from index 1 (rather than 0) finds the *real* separator for these entries.
        const int separatorIndex = raw.indexOf(QLatin1Char('='), 1);
        if (separatorIndex <= 0)
        {
            continue;
        }

        const QString name = raw.left(separatorIndex);
        if (name.startsWith(QLatin1Char('=')))
        {
            continue;
        }

        EnvironmentVariable variable;
        variable.name = name;
        variable.values = raw.mid(separatorIndex + 1).split(QLatin1Char(';'));
        variables.push_back(std::move(variable));
    }

    FreeEnvironmentStringsW(environmentBlock);
#endif

    return variables;
}
