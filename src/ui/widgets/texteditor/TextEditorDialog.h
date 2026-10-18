#pragma once

#include "TextEditorOptions.h"

#include <QDialog>

class TextEditorPanel;
class SyntaxHighlightEngine;
class HighlightThemeViewModel;

// Tools > Text Editor... (ToDo.md "Core editing" section): a standalone, non-file-backed dialog
// hosting TextEditorPanel. Deliberately bare for now -- no open/save/encoding handling yet, just
// a scratch pad to build the editor widget up from.
class TextEditorDialog : public QDialog
{
    Q_OBJECT

public:
    // highlightEngine/highlightThemeViewModel are threaded straight through to TextEditorPanel.
    explicit TextEditorDialog(const TextEditorOptions& options = TextEditorOptions{}, QWidget* parent = nullptr,
                               SyntaxHighlightEngine* highlightEngine = nullptr,
                               HighlightThemeViewModel* highlightThemeViewModel = nullptr);

private:
    TextEditorPanel* m_panel = nullptr;
};
