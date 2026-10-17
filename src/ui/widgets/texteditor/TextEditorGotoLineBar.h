#pragma once

#include <QWidget>

class QLineEdit;
class QEvent;
class TextEditorWidget;

// Go to Line overlay for TextEditorWidget (ToDo.md "Core editing" -- go to line), shown/hidden by
// TextEditorDialog via Ctrl+G. Floats over the editor's top-right corner the same way
// TextEditorFindBar does. Jumps live as the typed line number changes (so the cursor previews
// each candidate line without losing focus), and Enter additionally closes the bar; there's no
// Next/Previous state to keep around like the find bar has.
class TextEditorGotoLineBar : public QWidget
{
    Q_OBJECT

public:
    // editor also becomes this widget's parent, so the bar is a floating child drawn on top of
    // the editor's viewport instead of a widget in some enclosing layout.
    explicit TextEditorGotoLineBar(TextEditorWidget* editor);

    // Shows the bar if hidden, repositions it over the editor, rebuilds the line edit's validator
    // range from the editor's current block count (it can have changed since last use), prefills
    // it with the cursor's current line, then focuses and selects the text.
    void showAndFocus();

    // Hides the bar and returns focus to the editor.
    void hideAndClear();

protected:
    // Installed on m_editor (to reposition this overlay when the editor is resized) and on the
    // line edit (so Escape closes the bar while it has focus).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Re-anchors this widget to the editor viewport's top-right corner, clear of the vertical
    // scrollbar. Called on construction, on showAndFocus, and whenever the editor resizes.
    void repositionOverEditor();

    // Parses the line edit's text; on a valid 1-based line, jumps the editor there (without
    // stealing focus from the line edit, so typing keeps previewing further jumps). On
    // invalid/empty input, does nothing. closeAfter also hides the bar afterwards -- used for
    // Enter, not for live-as-you-type text changes.
    void jumpToEnteredLine(bool closeAfter);

    TextEditorWidget* m_editor = nullptr;
    QLineEdit* m_lineEdit = nullptr;
};
