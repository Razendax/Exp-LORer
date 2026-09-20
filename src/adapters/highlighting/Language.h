#pragma once

// The languages this pass of text-preview syntax highlighting supports (Architecture.md §14.26).
// Plain enum, no framework dependency -- LanguageRegistry maps a file extension to one of these,
// and SyntaxHighlightEngine owns one tree-sitter parser/query per value.
enum class Language
{
    C,
    Cpp,
    CSharp,
    Python,
    JavaScript,
    TypeScript,
    Json,
    Html,
    Css,
};
