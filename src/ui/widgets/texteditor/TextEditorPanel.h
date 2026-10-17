#pragma once

#include <QWidget>

class TextEditorWidget;
class TextEditorFindBar;
class TextEditorGotoLineBar;

// Standalone embeddable unit: TextEditorWidget plus its find bar and go-to-line overlay, wired up
// with Ctrl+F / Ctrl+G shortcuts and the "opening one bar closes the other" behavior. Usable
// directly in any layout (dialog, tab, dock, ...) -- see TextEditorDialog for the dialog-hosted
// case. Shortcuts are scoped to this widget's subtree (Qt::WidgetWithChildrenShortcut) rather than
// the whole window, so embedding this panel next to other widgets doesn't steal their Ctrl+F/Ctrl+G.
class TextEditorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TextEditorPanel(QWidget* parent = nullptr);

private:
    TextEditorWidget* m_editor = nullptr;
    TextEditorFindBar* m_findBar = nullptr;
    TextEditorGotoLineBar* m_gotoLineBar = nullptr;
};
