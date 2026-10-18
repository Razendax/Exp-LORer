#pragma once

#include "TextEditorOptions.h"

#include <QWidget>

class TextEditorWidget;
class TextEditorFindBar;
class TextEditorGotoLineBar;
class SyntaxHighlightEngine;
class HighlightThemeViewModel;

// Standalone embeddable unit: TextEditorWidget plus its find bar and go-to-line overlay, wired up
// with Ctrl+F / Ctrl+G shortcuts and the "opening one bar closes the other" behavior. Usable
// directly in any layout (dialog, tab, dock, ...) -- see TextEditorDialog for the dialog-hosted
// case. Shortcuts are scoped to this widget's subtree (Qt::WidgetWithChildrenShortcut) rather than
// the whole window, so embedding this panel next to other widgets doesn't steal their Ctrl+F/Ctrl+G.
class TextEditorPanel : public QWidget
{
    Q_OBJECT

public:
    // highlightEngine/highlightThemeViewModel are threaded straight through to TextEditorWidget
    // (see its constructor comment) -- this panel has no opinion on highlighting itself.
    explicit TextEditorPanel(const TextEditorOptions& options = TextEditorOptions{}, QWidget* parent = nullptr,
                              SyntaxHighlightEngine* highlightEngine = nullptr,
                              HighlightThemeViewModel* highlightThemeViewModel = nullptr);

    // Runtime toggle (Architecture.md §14.97): forwards to the editor and, if present, the find
    // bar (whose Replace row needs independent gating -- see TextEditorFindBar::setReadOnly).
    void setReadOnly(bool readOnly);

    // Lets an embedding site (TextEditPreviewWidget) drive the editor directly -- toPlainText()/
    // setPlainText()/document()->isModified()/document()->modificationChanged -- rather than this
    // panel growing a forwarding method for each one.
    TextEditorWidget* editor() const { return m_editor; }

private:
    TextEditorWidget* m_editor = nullptr;
    TextEditorFindBar* m_findBar = nullptr;
    TextEditorGotoLineBar* m_gotoLineBar = nullptr;
};
