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
    }
    return QString();
}
