#pragma once

#include <QPlainTextEdit>

class FilePreview;
class HighlightThemeViewModel;
class SyntaxHighlightEngine;
class SyntaxHighlighter;

// The text page of the Preview panel's stack (Architecture.md §14.26/§14.58, Specification.md
// "Preview Panel"): a read-only monospace view with syntax highlighting driven by
// SyntaxHighlightEngine/HighlightThemeViewModel.
class TextPreviewWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    TextPreviewWidget(SyntaxHighlightEngine& syntaxHighlightEngine, HighlightThemeViewModel& highlightThemeViewModel,
                       QWidget* parent = nullptr);

    void setPreview(const FilePreview& preview);

private:
    SyntaxHighlighter* m_syntaxHighlighter = nullptr;
};
