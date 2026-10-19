#include "LanguageRegistry.h"

#include <QObject>

std::optional<Language> LanguageRegistry::languageForExtension(const std::string& lowercaseExtension)
{
    if (lowercaseExtension == ".c")
    {
        return Language::C;
    }
    // .h/.hpp are ambiguous between C and C++ in general, but this codebase (like most C++
    // projects using a plain .h extension) always means C++ -- parsing a real C++ header with the
    // plain-C grammar would misrender templates/classes/namespaces, so .h/.hpp go to Cpp.
    if (lowercaseExtension == ".h" || lowercaseExtension == ".hpp" || lowercaseExtension == ".cpp" ||
        lowercaseExtension == ".cc" || lowercaseExtension == ".cxx")
    {
        return Language::Cpp;
    }
    if (lowercaseExtension == ".cs")
    {
        return Language::CSharp;
    }
    if (lowercaseExtension == ".py")
    {
        return Language::Python;
    }
    if (lowercaseExtension == ".js")
    {
        return Language::JavaScript;
    }
    if (lowercaseExtension == ".ts")
    {
        return Language::TypeScript;
    }
    if (lowercaseExtension == ".json")
    {
        return Language::Json;
    }
    if (lowercaseExtension == ".html" || lowercaseExtension == ".htm")
    {
        return Language::Html;
    }
    if (lowercaseExtension == ".css")
    {
        return Language::Css;
    }
    if (lowercaseExtension == ".yaml" || lowercaseExtension == ".yml")
    {
        return Language::Yaml;
    }
    if (lowercaseExtension == ".ps1" || lowercaseExtension == ".psm1" || lowercaseExtension == ".psd1")
    {
        return Language::PowerShell;
    }
    if (lowercaseExtension == ".md")
    {
        return Language::Markdown;
    }
    return std::nullopt;
}

QString LanguageRegistry::displayName(Language language)
{
    switch (language)
    {
        case Language::C:
            return QObject::tr("C");
        case Language::Cpp:
            return QObject::tr("C++");
        case Language::CSharp:
            return QObject::tr("C#");
        case Language::Python:
            return QObject::tr("Python");
        case Language::JavaScript:
            return QObject::tr("JavaScript");
        case Language::TypeScript:
            return QObject::tr("TypeScript");
        case Language::Json:
            return QObject::tr("JSON");
        case Language::Html:
            return QObject::tr("HTML");
        case Language::Css:
            return QObject::tr("CSS");
        case Language::Yaml:
            return QObject::tr("YAML");
        case Language::PowerShell:
            return QObject::tr("PowerShell");
        case Language::Markdown:
            return QObject::tr("Markdown");
    }
    return QString();
}
